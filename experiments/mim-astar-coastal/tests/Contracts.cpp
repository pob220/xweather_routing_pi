// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <future>
#include <iostream>
#include <stdexcept>

#include "Fixtures.h"
#include "mim_coastal/Router.h"
#include "GeographyHints.h"

namespace {
namespace wr = supercpn::weather_routing;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct TestCase {
  Json::Value scenario{fixture::scenario("contract")};
  wr::RoutingRequest request;
  wr::RoutingEnvironment environment;
  mim_coastal::Options options;

  void prepare() {
    request = fixture::configure(scenario);
    environment.grib =
        std::make_shared<fixture::Weather>(scenario, request.start);
    environment.landAndBoundaries =
        std::make_shared<fixture::Land>(scenario, request.start);
    request.cancellation = request.cancellation.bounded(
        std::chrono::steady_clock::now() + std::chrono::seconds{15}, {});
    options.certifiedMaximumGroundSpeedKnots = 6;
  }
  mim_coastal::Result solve() const {
    return mim_coastal::Router{}.route(request, environment, options);
  }
  void audit(const mim_coastal::Result& result) const {
    require(result.route.status == wr::RoutingStatus::Complete,
            "Expected complete: " + result.route.message + " / " +
                result.search.stopReason);
    require(result.route.validation.passed, "Internal validation missing");
    auto independentRequest = request;
    independentRequest.cancellation = {};
    wr::PolarPerformanceModel performance(request.vessel);
    const auto replay = wr::RouteValidator{}.validate(
        independentRequest, environment, performance, result.route.legs);
    require(replay.passed,
            "Fresh independent replay failed: " + replay.failureReason);
    require(result.route.legs.back().endTime <=
                request.departure + request.limits.maximumRouteDuration,
            "Arrival exceeded horizon");
  }
};

void analytic() {
  for (bool coastal : {false, true}) {
    TestCase test;
    test.scenario["isotropic"] = true;
    test.scenario["east_nm"] = 2;
    test.prepare();
    test.options.coastEverywhere = coastal;
    const auto result = test.solve();
    test.audit(result);
    const double expected =
        wr::distanceNm(test.request.start, test.request.destination) / 6 * 3600;
    require(std::abs(result.route.metrics.elapsed.count() - expected) <= 2,
            "Constant-speed oracle mismatch");
  }
  TestCase dateline;
  dateline.scenario["start"][1] = 179.99;
  dateline.scenario["isotropic"] = true;
  dateline.scenario["east_nm"] = 3;
  dateline.prepare();
  dateline.audit(dateline.solve());
}

void geography() {
  TestCase test;
  test.scenario["isotropic"] = true;
  test.scenario["east_nm"] = 8;
  for (double value : {2., -1., 4., 1.})
    test.scenario["rectangles_nm"][0].append(value);
  test.prepare();
  test.request.options.allowWaiting = false;

  const auto local = test.solve();
  test.audit(local);
  require(local.search.astarMotionAttempts > 0,
          "Near-land search did not use coastal moves");
  TestCase handoff;
  handoff.scenario["isotropic"] = true;
  handoff.scenario["east_nm"] = 40;
  for (double v : {15., -2., 20., 2.})
    handoff.scenario["rectangles_nm"][0].append(v);
  handoff.prepare();
  const auto mixed = handoff.solve();
  handoff.audit(mixed);
  require(mixed.search.mimMotionAttempts > 0 &&
              mixed.search.astarMotionAttempts > 0,
          "Ocean/coastal history handoff was not exercised");
  require(mixed.search.deferredResumed > 0,
          "Deferred alternatives were never revisited");
}

void waiting() {
  TestCase test;
  test.scenario["isotropic"] = true;
  test.scenario["east_nm"] = 8;
  test.scenario["gate"] = true;
  test.scenario["gate_east_nm"] = 3;
  test.scenario["gate_opens_hours"] = 2;
  for (const auto& rectangle :
       {std::array{-10., .05, 20., 20.}, std::array{-10., -20., 20., -.05},
        std::array{-10., -20., -.1, 20.}}) {
    Json::Value coordinates(Json::arrayValue);
    for (double value : rectangle) coordinates.append(value);
    test.scenario["rectangles_nm"].append(coordinates);
  }
  test.prepare();
  test.options.coastEverywhere = true;
  test.options.headingStepDegrees = 90;
  test.options.coastalHeadingStepDegrees = 90;
  test.options.coastalStep = std::chrono::hours{1};

  const auto waited = test.solve();
  test.audit(waited);
  require(waited.route.metrics.waitingTime > wr::Duration::zero(),
          "Timed corridor requires explicit charged waiting");
  // Fine steps can advance to the gate before waiting. Charging two hours at
  // departure would incorrectly forbid that better physical solution.
  require(waited.route.metrics.elapsed >=
              std::chrono::minutes{170} - wr::Duration{2},
          "Route crossed the timed gate before it opened");
  // Waiting is a real, checked, charged action, not a zero-cost edge.
  TestCase calm;
  calm.scenario["wind_speed"] = 0;
  calm.scenario["motor"] = true;
  calm.scenario["east_nm"] = 2;
  calm.prepare();
  calm.audit(calm.solve());
}

void acceptance() {
  TestCase rejected;
  rejected.scenario["east_nm"] = 2;
  rejected.scenario["validation_block"] = true;
  rejected.prepare();
  rejected.options.maximumMotionAttempts = 300;
  const auto result = rejected.solve();
  require(!result.route.validation.passed && result.route.legs.empty(),
          "Replay rejection became completion");
  TestCase missing;
  missing.scenario["missing_current"] = true;
  missing.scenario["currents"] = true;
  missing.prepare();
  require(!missing.solve().route.validation.passed,
          "Missing required current accepted");
  TestCase horizon;
  horizon.scenario["east_nm"] = 8;
  horizon.scenario["max_hours"] = 0.25;
  horizon.prepare();
  require(!horizon.solve().route.validation.passed,
          "Beyond-horizon arrival accepted");
}

void resources() {
  TestCase invalidStrategy;
  invalidStrategy.prepare();
  invalidStrategy.options.coastalStep = wr::Duration{-1};
  require(invalidStrategy.solve().route.status ==
              wr::RoutingStatus::InvalidVesselConfiguration,
          "Invalid coastal step was not rejected at the API boundary");
  TestCase test;
  test.prepare();
  test.options.maximumMotionAttempts = 1;
  auto result = test.solve();
  require(result.route.status == wr::RoutingStatus::ResourceLimitReached,
          "Motion budget ignored");
  test.options.maximumMotionAttempts = 1000;
  test.options.searchMemoryBytes = 1024;
  result = test.solve();
  require(result.route.status == wr::RoutingStatus::ResourceLimitReached,
          "Memory budget ignored");
  test.options.searchMemoryBytes = 64 * 1024 * 1024;
  test.request.cancellation.cancel();
  result = test.solve();
  require(result.route.status == wr::RoutingStatus::Cancelled &&
              result.route.legs.empty(),
          "Cancellation ignored");
}

void guidance() {
  TestCase test;
  test.scenario["isotropic"] = true;
  test.scenario["east_nm"] = 8;
  for (double v : {2., -1., 4., 1.})
    test.scenario["rectangles_nm"][0].append(v);
  test.prepare();
  // An intentionally misleading cheap guide omits the island. It must never
  // replace the authoritative physical checks or turn a crossing into success.
  test.options.geographyGuidance = std::make_shared<wr::OpenWaterProvider>();
  test.audit(test.solve());
  test.options.coastEverywhere = true;
  test.options.coastalFanBatch = 1;
  const auto partial = test.solve();
  test.audit(partial);
  require(partial.search.deferredResumed > 0,
          "Unfinished coastal fans were never resumed");
  test.options.coastEverywhere = false;
  mim_coastal::Diagnostics diagnostics;
  mim_coastal::detail::GeographyHints hints(test.request, test.environment,
                                            test.options, diagnostics);
  for (int i = 0; i < 20; ++i) hints.coastal(test.request.start);
  require(diagnostics.hintCacheHits >= 19 && diagnostics.hintQueries == 1,
          "Repeated open-ocean hints did not use one cached provider query");
  test.options.maximumGuidanceQueries = 1;
  mim_coastal::Diagnostics limited;
  test.options.geographyGuidance = test.environment.landAndBoundaries;
  mim_coastal::detail::GeographyHints capped(test.request, test.environment,
                                             test.options, limited);
  capped.coastal(test.request.start);
  require(limited.hintQueries <= 1, "Guidance query allowance exceeded");
  require(!capped.coastal(wr::destinationPoint(test.request.start, 270, 50)),
          "Exhausted hint allowance was misclassified as coastal water");
  require(limited.hintUnavailable > 0,
          "Unknown-hint fallback was not reported");
  require(limited.guidanceCells <= test.options.guidanceGridCells,
          "Guidance grid grew without bound");
  TestCase bound;
  bound.scenario["isotropic"] = true;
  bound.scenario["east_nm"] = 2;
  bound.prepare();
  bound.options.certifiedMaximumGroundSpeedKnots = 1;
  require(bound.solve().route.status ==
              wr::RoutingStatus::InvalidVesselConfiguration,
          "Invalid ground-speed bound was accepted");
}

void observers() {
  TestCase test;
  test.scenario["east_nm"] = 2;
  test.prepare();
  test.options.onIncumbent = [](const wr::RoutingResult&) {
    throw std::runtime_error("observer");
  };
  const auto result = test.solve();
  test.audit(result);
  require(result.search.observerErrors > 0, "Observer exception not recorded");
  test.options.onIncumbent = [&](const wr::RoutingResult&) {
    test.request.cancellation.cancel();
  };
  const auto cancelled = test.solve();
  require(cancelled.route.status == wr::RoutingStatus::Cancelled &&
              cancelled.route.legs.empty() &&
              !cancelled.route.validation.passed,
          "Post-incumbent cancellation delivered stale route");
}

void concurrency() {
  auto solve = [] {
    TestCase test;
    test.scenario["isotropic"] = true;
    test.scenario["east_nm"] = 2;
    test.prepare();
    const auto result = test.solve();
    test.audit(result);
    return result.route.metrics.elapsed;
  };
  auto first = std::async(std::launch::async, solve);
  auto second = std::async(std::launch::async, solve);
  require(first.get() == second.get(),
          "Concurrent independent requests differ");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string group = argc > 1 ? argv[1] : "analytic";
    if (group == "analytic")
      analytic();
    else if (group == "geography")
      geography();
    else if (group == "waiting")
      waiting();
    else if (group == "acceptance")
      acceptance();
    else if (group == "resources")
      resources();
    else if (group == "guidance")
      guidance();
    else if (group == "observers")
      observers();
    else if (group == "concurrency")
      concurrency();
    else
      throw std::runtime_error("Unknown test group");
    std::cout << "PASS " << group << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
