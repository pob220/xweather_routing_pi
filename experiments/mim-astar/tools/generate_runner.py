#!/usr/bin/env python3
"""Extend the existing six-engine measurement client, without editing its tree.

The generated runner keeps provider construction, timers and the fresh external
audit identical. Only prototype dispatch and extra diagnostic fields are added.
Fail on source drift instead of silently generating an incomplete comparison.
"""
from pathlib import Path
import sys


def replace_once(source, anchor, replacement):
    if source.count(anchor) != 1:
        raise RuntimeError(f"Benchmark source changed: expected one {anchor!r}")
    return source.replace(anchor, replacement)


source = Path(sys.argv[1]).read_text()
source = replace_once(source, '#include "Fixtures.h"',
                      '#include "Fixtures.h"\n#include "mim_astar/Router.h"\n#include "BenchmarkBounds.h"\n#include "HeadlessFixtures.h"')
source = replace_once(source, '    auto raw = j;',
                      '    headless_fixture::configure(j, r);\n    auto raw = j;')
source = replace_once(source, '    const bool profileProviders =', r'''
    if (j.isMember("headless_suite")) {
      if (!j.isMember("grib"))
        source = std::make_shared<headless_fixture::LabWeather>(raw, r.departure);
      source = std::make_shared<headless_fixture::AdjustedWeather>(
          source, j.get("wind_strength", 1.).asDouble(),
          j.get("host_timeline_bounds", false).asBool());
      relative = r.environment.useCurrent;
      if (j["headless_suite"] == "professional") {
        const auto coverage = source->windCoverage();
        if (coverage.ends)
          r.limits.maximumRouteDuration = std::min(
              r.limits.maximumRouteDuration, *coverage.ends - r.departure);
      }
    }
    const bool profileProviders =''')
source = replace_once(source, '      if (engine == "atlas") {', r'''
      mim_astar::Diagnostics mimDiagnostics;
      if (engine == "mim" || engine == "astar" || engine == "mim_astar" ||
          engine == "mim_astar_zero" || engine == "astar_zero") {
        mim_astar::Options options;
        if (engine == "mim") options.strategy = mim_astar::Strategy::MimOnly;
        if (engine == "astar" || engine == "astar_zero")
          options.strategy = mim_astar::Strategy::AStarOnly;
        options.certifiedMaximumGroundSpeedKnots =
            benchmarkGroundSpeedBound(request, j).value_or(0.0);
        if (engine == "mim_astar_zero" || engine == "astar_zero")
          options.certifiedMaximumGroundSpeedKnots = 0;
        options.maximumMotionAttempts = j.get("mim_motions", Json::UInt64(options.maximumMotionAttempts)).asUInt64();
        options.maximumImprovementExpansions = j.get("mim_improvement", options.maximumImprovementExpansions).asUInt();
        options.headingStepDegrees = j.get("mim_heading_step", options.headingStepDegrees).asDouble();
        options.cellSizeNm = j.get("mim_cell_nm", options.cellSizeNm).asDouble();
        options.exactStateKeys = j.get("mim_exact_keys", options.exactStateKeys).asBool();
        auto result = mim_astar::Router{}.route(request, env, options);
        route = std::move(result.route);
        mimDiagnostics = std::move(result.search);
      } else if (engine == "atlas") {''')
source = replace_once(source, '      v["case"] = j["name"];', r'''
      if (engine == "mim" || engine == "astar" || engine == "mim_astar" ||
          engine == "mim_astar_zero" || engine == "astar_zero") {
        auto& diagnostics = v["mim_diagnostics"];
        diagnostics["motion_attempts"] = Json::UInt64(mimDiagnostics.motionAttempts);
        diagnostics["mim_motions"] = Json::UInt64(mimDiagnostics.mimMotionAttempts);
        diagnostics["astar_motions"] = Json::UInt64(mimDiagnostics.astarMotionAttempts);
        diagnostics["connections"] = Json::UInt64(mimDiagnostics.connectionAttempts);
        diagnostics["expanded_labels"] = Json::UInt64(mimDiagnostics.expandedLabels);
        diagnostics["retained_labels"] = Json::UInt64(mimDiagnostics.retainedLabels);
        diagnostics["cell_pruned"] = Json::UInt64(mimDiagnostics.cellPruned);
        diagnostics["bound_pruned"] = Json::UInt64(mimDiagnostics.boundPruned);
        diagnostics["managed_peak_bytes"] = Json::UInt64(mimDiagnostics.peakManagedBytes);
        diagnostics["first_incumbent_seconds"] = mimDiagnostics.firstIncumbentSeconds;
        diagnostics["improvements"] = Json::UInt64(mimDiagnostics.improvements);
        diagnostics["certified_bound"] = mimDiagnostics.usesCertifiedBound;
        diagnostics["approximate_pruning"] = mimDiagnostics.approximatePruning;
        diagnostics["resource_limited"] = mimDiagnostics.resourceLimited;
        diagnostics["stop_reason"] = mimDiagnostics.stopReason;
        diagnostics["mim_yield_reason"] = mimDiagnostics.mimYieldReason;
      }
      v["case"] = j["name"];''')
Path(sys.argv[2]).write_text(source)
