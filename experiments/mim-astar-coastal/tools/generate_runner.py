#!/usr/bin/env python3
"""Add only dispatch/diagnostics to the unchanged experiment-1 comparison client."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()

def replace(anchor, value):
    global source
    if source.count(anchor) != 1:
        raise RuntimeError(f"Comparison client drift at {anchor!r}")
    source = source.replace(anchor, value)

replace('#include "mim_astar/Router.h"', '#include "mim_astar/Router.h"\n#include "mim_coastal/Router.h"')
replace('      mim_astar::Diagnostics mimDiagnostics;', '''
      mim_coastal::Diagnostics coastalDiagnostics;
      mim_astar::Diagnostics mimDiagnostics;''')
replace('      mim_astar::Diagnostics mimDiagnostics;\n      if (engine == "mim" || engine == "astar" || engine == "mim_astar" ||', '''
      mim_astar::Diagnostics mimDiagnostics;
      if (engine == "mim_coastal" || engine == "coastal_astar" ||
          engine == "mim_coastal_no_hints" || engine == "mim_coastal_stall_recovery" ||
          engine == "mim_coastal_improve") {
        mim_coastal::Options options;
        options.certifiedMaximumGroundSpeedKnots = benchmarkGroundSpeedBound(request, j).value_or(0.0);
        options.coastEverywhere = engine == "coastal_astar";
        options.guidanceEnabled = engine != "mim_coastal_no_hints";
        options.continueAfterFirstRoute = engine == "mim_coastal_improve";
        if (engine == "mim_coastal_stall_recovery") options.deferredServicePeriod = 0;
        options.guidanceWeight = j.get("coastal_guidance_weight", options.guidanceWeight).asDouble();
        options.coastalFanBatch = j.get("coastal_fan_batch", options.coastalFanBatch).asUInt();
        options.coastalBandNm = j.get("coastal_band_nm", options.coastalBandNm).asDouble();
        options.coastalStep = wr::Duration{j.get("coastal_step_seconds", int(options.coastalStep.count())).asInt()};
        options.guidanceGridCells = j.get("coastal_grid_cells", options.guidanceGridCells).asUInt();
        options.maximumMotionAttempts = j.get("coastal_motions", Json::UInt64(options.maximumMotionAttempts)).asUInt64();
        options.maximumLabels = j.get("coastal_labels", Json::UInt64(options.maximumLabels)).asUInt64();
        options.headingStepDegrees = j.get("coastal_ocean_heading_step", options.headingStepDegrees).asDouble();
        options.coastalHeadingStepDegrees = j.get("coastal_heading_step", options.coastalHeadingStepDegrees).asDouble();
        options.geographyGuidance = env.landAndBoundaries;
        if (j.isMember("guidance_shoreline"))
          options.geographyGuidance = std::make_shared<original_routing::GshhgProvider>(j["guidance_shoreline"].asString());
        auto result = mim_coastal::Router{}.route(request, env, options);
        route = std::move(result.route);
        coastalDiagnostics = std::move(result.search);
      } else if (engine == "mim" || engine == "astar" || engine == "mim_astar" ||''')
# The first anchor appears in dispatch and the v1 diagnostics, so use a distinct
# single-line output anchor for the new result's independent measurements.
replace('      v["case"] = j["name"];', '''
      if (engine.rfind("mim_coastal", 0) == 0 || engine == "coastal_astar") {
        auto& d = v["coastal_diagnostics"];
        d["closest_nm"] = std::isfinite(route.diagnostics.closestApproachNm)
            ? Json::Value(route.diagnostics.closestApproachNm) : Json::Value();
        d["motion_attempts"] = Json::UInt64(coastalDiagnostics.motionAttempts);
        d["ocean_motions"] = Json::UInt64(coastalDiagnostics.mimMotionAttempts);
        d["coastal_motions"] = Json::UInt64(coastalDiagnostics.astarMotionAttempts);
        d["ocean_layers"] = Json::UInt64(coastalDiagnostics.mimLayers);
        d["coastal_expansions"] = Json::UInt64(coastalDiagnostics.expandedLabels);
        d["deferred"] = Json::UInt64(coastalDiagnostics.deferredLabels);
        d["resumed"] = Json::UInt64(coastalDiagnostics.deferredResumed);
        d["coastal_transitions"] = Json::UInt64(coastalDiagnostics.coastalTransitions);
        d["blocked_ocean_escalations"] = Json::UInt64(coastalDiagnostics.blockedOceanEscalations);
        d["hint_unavailable"] = Json::UInt64(coastalDiagnostics.hintUnavailable);
        d["hint_queries"] = Json::UInt64(coastalDiagnostics.hintQueries);
        d["hint_cache_hits"] = Json::UInt64(coastalDiagnostics.hintCacheHits);
        d["hint_cache_cells"] = Json::UInt64(coastalDiagnostics.hintCacheCells);
        d["guidance_cells"] = Json::UInt64(coastalDiagnostics.guidanceCells);
        d["guidance_build_seconds"] = coastalDiagnostics.guidanceBuildSeconds;
        d["retained_labels"] = Json::UInt64(coastalDiagnostics.retainedLabels);
        d["managed_peak_bytes"] = Json::UInt64(coastalDiagnostics.peakManagedBytes);
        d["first_valid_seconds"] = coastalDiagnostics.firstIncumbentSeconds;
        d["improvements"] = Json::UInt64(coastalDiagnostics.improvements);
        d["stop_reason"] = coastalDiagnostics.stopReason;
      }
      v["case"] = j["name"];''')
Path(sys.argv[2]).write_text(source)
