// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <future>
#include <iostream>
#include <stdexcept>

#include "Fixtures.h"
#include "mim_astar/Router.h"

namespace {
namespace wr = supercpn::weather_routing;
using mim_astar::Strategy;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct TestCase {
  Json::Value scenario{fixture::scenario("contract")};
  wr::RoutingRequest request;
  wr::RoutingEnvironment environment;
  mim_astar::Options options;

  void prepare() {
    request = fixture::configure(scenario);
    environment.grib = std::make_shared<fixture::Weather>(scenario, request.start);
    environment.landAndBoundaries =
        std::make_shared<fixture::Land>(scenario, request.start);
    request.cancellation = request.cancellation.bounded(
        std::chrono::steady_clock::now() + std::chrono::seconds{15}, {});
    options.certifiedMaximumGroundSpeedKnots = 6;
  }
  mim_astar::Result solve() const {
    return mim_astar::Router{}.route(request, environment, options);
  }
  void audit(const mim_astar::Result& result) const {
    require(
        result.route.status == wr::RoutingStatus::Complete,
        "Expected complete: " + result.route.message + " / " + result.search.stopReason);
    require(result.route.validation.passed, "Internal validation missing");
    auto independentRequest = request;
    independentRequest.cancellation = {};
    wr::PolarPerformanceModel performance(request.vessel);
    const auto replay = wr::RouteValidator{}.validate(independentRequest, environment,
                                                      performance, result.route.legs);
    require(replay.passed, "Fresh independent replay failed: " + replay.failureReason);
    require(result.route.legs.back().endTime <=
                request.departure + request.limits.maximumRouteDuration,
            "Arrival exceeded horizon");
  }
};

void analytic() {
  for (auto strategy : {Strategy::MimOnly, Strategy::AStarOnly, Strategy::MimThenAStar}) {
    TestCase test;
    test.scenario["isotropic"] = true;
    test.scenario["east_nm"] = 2;
    test.prepare();
    test.options.strategy = strategy;
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
  for (double value : {2., -1., 4., 1.}) test.scenario["rectangles_nm"][0].append(value);
  test.prepare();
  test.request.options.allowWaiting = false;
  test.options.maximumImprovementExpansions = 50;
  test.audit(test.solve());
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
  test.options.strategy = Strategy::AStarOnly;
  test.options.headingStepDegrees = 90;
  test.options.adaptiveSteps = false;
  test.options.maximumImprovementExpansions = 20;
  const auto waited = test.solve();
  test.audit(waited);
  require(waited.route.metrics.waitingTime >= std::chrono::hours{2},
          "Timed corridor requires explicit charged waiting");
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
  require(!missing.solve().route.validation.passed, "Missing required current accepted");
  TestCase horizon;
  horizon.scenario["east_nm"] = 8;
  horizon.scenario["max_hours"] = 0.25;
  horizon.prepare();
  require(!horizon.solve().route.validation.passed, "Beyond-horizon arrival accepted");
}

void resources() {
  TestCase invalidStrategy;
  invalidStrategy.prepare();
  invalidStrategy.options.strategy = static_cast<Strategy>(255);
  require(invalidStrategy.solve().route.status ==
              wr::RoutingStatus::InvalidVesselConfiguration,
          "Unknown strategy was not rejected at the API boundary");
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
  require(
      result.route.status == wr::RoutingStatus::Cancelled && result.route.legs.empty(),
      "Cancellation ignored");
}

void bounds() {
  TestCase test;
  test.scenario["isotropic"] = true;
  test.scenario["east_nm"] = 2;
  test.prepare();
  test.options.strategy = Strategy::AStarOnly;
  test.options.exactStateKeys = true;
  const auto informed = test.solve();
  test.options.certifiedMaximumGroundSpeedKnots = 0;
  const auto uninformed = test.solve();
  test.audit(informed);
  test.audit(uninformed);
  require(informed.route.metrics.elapsed == uninformed.route.metrics.elapsed,
          "Bound changed analytic optimum");
  require(!informed.search.approximatePruning && !uninformed.search.usesCertifiedBound,
          "Incorrect policy diagnostics");
  test.options.certifiedMaximumGroundSpeedKnots = 1;
  const auto wrong = test.solve();
  require(wrong.route.status == wr::RoutingStatus::InvalidVesselConfiguration &&
              wrong.route.legs.empty(),
          "Observed speed-bound violation accepted");
}

void improvement() {
  TestCase test;
  test.prepare();
  test.request.options.allowWaiting = false;
  test.options.strategy = Strategy::MimOnly;
  const auto first = test.solve();
  test.audit(first);
  std::vector<wr::Duration> arrivals;
  test.options.strategy = Strategy::MimThenAStar;
  test.options.onIncumbent = [&](const wr::RoutingResult& incumbent) {
    require(arrivals.empty() || incumbent.metrics.elapsed < arrivals.back(),
            "Incumbent observer regressed");
    arrivals.push_back(incumbent.metrics.elapsed);
  };
  const auto refined = test.solve();
  test.audit(refined);
  require(refined.route.metrics.elapsed <= first.route.metrics.elapsed,
          "Hybrid discarded a better MIM incumbent");
  require(refined.search.astarMotionAttempts > 0, "Hybrid skipped A* improvement");
  require(refined.search.observerErrors == 0,
          "Incumbent sequence was not strictly improving");
}

void stageBudgets() {
  TestCase test;
  test.scenario["east_nm"] = 300;
  test.scenario["step_seconds"] = 21600;
  test.prepare();
  test.options.maximumWeatherSamples = 1000;
  const auto weatherLimited = test.solve();
  require(weatherLimited.search.mimMotionAttempts > 0 &&
              weatherLimited.search.astarMotionAttempts > 0,
          "MIM consumed all weather work before A* could run");
  require(weatherLimited.search.mimYieldReason.find("weather") != std::string::npos,
          "Weather reservation was not reported");
  test.options.maximumWeatherSamples = 1000000;
  test.options.maximumLabels = 8;
  const auto labelLimited = test.solve();
  require(labelLimited.search.astarMotionAttempts > 0 &&
              labelLimited.search.mimYieldReason.find("lineage") != std::string::npos,
          "MIM lineage exhaustion prevented independent A* restart");
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
              cancelled.route.legs.empty() && !cancelled.route.validation.passed,
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
  require(first.get() == second.get(), "Concurrent independent requests differ");
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
    else if (group == "bounds")
      bounds();
    else if (group == "improvement")
      improvement();
    else if (group == "stage_budgets")
      stageBudgets();
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
