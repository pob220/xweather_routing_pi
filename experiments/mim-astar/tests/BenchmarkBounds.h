// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "supercpn/weather_routing/Engine.h"

// Only for this benchmark's exact native PolarPerformanceModel and its known
// interpolation rules. This is not a certificate for arbitrary host providers.
inline std::optional<double> benchmarkGroundSpeedBound(
    const supercpn::weather_routing::RoutingRequest& request,
    const Json::Value& scenario) {
  double waterSpeed = request.vessel.propulsion.configuredMotorSpeedKnots;
  const double efficiency =
      std::max(request.vessel.upwindEfficiency, request.vessel.downwindEfficiency);
  for (const auto& profile : request.vessel.profiles) {
    double waveFactor = 1;
    for (const auto& wave : profile.wavePerformance)
      waveFactor = std::max(waveFactor, wave.speedFactor);
    for (const auto& row : profile.rows)
      for (const auto& point : row.points)
        waterSpeed = std::max(waterSpeed, point.boatSpeedKnots * profile.efficiency *
                                              efficiency * waveFactor);
  }
  waterSpeed += std::max(0.0, request.vessel.propulsion.motorSailingBoostKnots);
  double currentSpeed = 0;
  if (request.environment.useCurrent) {
    if (scenario.isMember("grib")) {
      if (!scenario.isMember("mim_current_bound_knots")) return {};
      // prepare_inputs.py computes a global component envelope over every
      // decoded current frame and fingerprints the source GRIB in bounds.json.
      currentSpeed = scenario["mim_current_bound_knots"].asDouble();
    } else {
      currentSpeed = std::abs(scenario.get("current_speed", 0.).asDouble()) +
                     (scenario.get("jet", false).asBool() ? 8.0 : 0.0);
      if (scenario.isMember("headless_suite") && scenario.get("tidal", false).asBool())
        currentSpeed += 3.;  // The older lab adds, rather than scales, this tide.
    }
  }
  const double bound = waterSpeed + currentSpeed;
  if (!std::isfinite(bound) || bound <= 0 || currentSpeed < 0) return {};
  return bound;
}
