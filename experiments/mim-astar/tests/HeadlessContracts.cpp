// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <stdexcept>

#include "BenchmarkBounds.h"
#include "HeadlessFixtures.h"

namespace wr = fixture::wr;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  try {
    auto scenario = fixture::scenario("import-contract");
    scenario["headless_suite"] = "comparison_lab";
    scenario["currents"] = true;
    scenario["current_speed"] = 1.;
    scenario["tidal"] = true;
    scenario["variable"] = true;
    auto request = fixture::configure(scenario);
    headless_fixture::configure(scenario, request);
    require(!request.options.allowWaiting, "Historical anchoring defaults off");
    require(request.options.destinationToleranceNm == .35, "Goal tolerance");
    require(request.options.headingStepDegrees == 10, "Heading step");
    auto source =
        std::make_shared<headless_fixture::LabWeather>(scenario, request.departure);
    const auto peak = request.departure + wr::Duration{11178};
    const auto current = source->current(request.start, peak).velocity;
    require(std::abs(std::hypot(current.eastKnots, current.northKnots) - 4) < 1e-6,
            "The imported tide adds its amplitude to the base current");
    require(*benchmarkGroundSpeedBound(request, scenario) >= 10, "Tide speed envelope");
    const auto hours = (peak - request.departure).count() / 3600.;
    const auto expected = wr::speedDirectionToVector(
        15 + 2 * std::sin(hours / 8),
        180 + 35 * std::sin(hours / 12 + request.start.longitude * .2));
    const auto actual = source->wind(request.start, peak).velocity;
    require(std::hypot(actual.eastKnots - expected.eastKnots,
                       actual.northKnots - expected.northKnots) < 1e-10,
            "Historical variable weather formula");
    headless_fixture::AdjustedWeather adjusted(source, .95, true);
    require(!adjusted.windCoverage().ends, "Omit time metadata only when explicit");
    require(!adjusted.wind(request.start, request.departure + std::chrono::hours{961})
                 .available,
            "Omitting metadata never extends forecast samples");
    scenario["headless_suite"] = "professional";
    scenario["waves"] = true;
    scenario["allow_missing_current_waves"] = true;
    headless_fixture::configure(scenario, request);
    require(request.environment.useWaves && request.environment.missingWavesAcknowledged,
            "Explicit historical missing-wave policy");
    require(request.options.destinationToleranceNm == .25 &&
                request.options.routingEffortPercent == 400,
            "Professional qualification defaults");
    require(request.limits.maximumForwardGeneratedStates > 0, "Stage resource policy");
    std::cout << "Headless import contracts passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
