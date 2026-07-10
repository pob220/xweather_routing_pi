/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "HeadlessRouteRunner.h"

#include <cstdlib>

#include <wx/wx.h>
#include <wx/datetime.h>

#include "RouteMapOverlay.h"
#include "RoutingScenarioJson.h"

namespace {

wxString EnvString(const char* name) {
  const char* value = getenv(name);
  return value ? wxString::FromUTF8(value) : wxString();
}

weather_routing_engine::RoutingCandidateResult CandidateFromRoute(
    RouteMapOverlay* route) {
  weather_routing_engine::RoutingCandidateResult candidate;
  if (!route) {
    candidate.state = "missing";
    candidate.finalSafety = "unknown";
    candidate.failureReason = "missing route";
    return candidate;
  }

  RouteMapConfiguration configuration = route->GetConfiguration();
  candidate.departure = configuration.StartTime;
  candidate.offsetMinutes = configuration.DepartureTimeOptimizationOffsetMinutes;
  candidate.eta = route->EndTime();
  candidate.distanceNm = route->RouteInfo(RouteMapOverlay::DISTANCE);
  if (candidate.eta.IsValid() && configuration.StartTime.IsValid()) {
    wxTimeSpan elapsed = candidate.eta - configuration.StartTime;
    candidate.elapsedSeconds = elapsed.GetSeconds().ToLong();
  }

  if (route->Finished() && route->ReachedDestination()) {
    candidate.state = "complete";
    candidate.finalSafety = "pass";
  } else if (route->Finished()) {
    candidate.state = "failed";
    candidate.finalSafety = "fail";
  } else {
    candidate.state = "incomplete";
    candidate.finalSafety = "unknown";
  }
  candidate.failureReason = route->GetFailureReason();
  candidate.reverseRecoveryUsed = configuration.ReverseRecoveryUsed;
  candidate.reverseRecoveryStatus = configuration.ReverseRecoveryStatus;
  candidate.reverseLayersBuilt = configuration.ReverseLayersBuilt;
  candidate.reverseNodesGenerated = configuration.ReverseNodesGenerated;
  candidate.reverseNodesFeasible = configuration.ReverseNodesFeasible;
  candidate.reverseConnectionFound = configuration.ReverseConnectionFound;
  candidate.reverseConnectionTime = configuration.ReverseConnectionTime;
  candidate.reverseFailureReason = configuration.ReverseFailureReason;
  candidate.reverseFinalValidationPass = configuration.ReverseFinalValidationPass;
  return candidate;
}

}  // namespace

namespace weather_routing_headless {

bool HeadlessRouteRunner::LoadScenarioFromEnv(
    weather_routing_engine::RoutingScenario& scenario,
    wxString& scenarioPath,
    wxString& outputPath,
    wxString& error) {
  scenarioPath = EnvString("WR_HEADLESS_SCENARIO");
  outputPath = EnvString("WR_HEADLESS_OUTPUT");
  if (scenarioPath.IsEmpty()) {
    error = "WR_HEADLESS_SCENARIO is not set";
    return false;
  }
  return LoadRoutingScenarioJson(scenarioPath, scenario, error);
}

bool HeadlessRouteRunner::WriteStartedResult(
    const wxString& outputPath,
    const weather_routing_engine::RoutingScenario& scenario,
    wxString& error) {
  if (outputPath.IsEmpty()) return true;
  weather_routing_engine::RoutingResult result;
  result.scenario = scenario.name;
  result.status = "running";
  return SaveRoutingResultJson(outputPath, result, error);
}

bool HeadlessRouteRunner::WriteSingleRouteResult(
    const wxString& outputPath,
    const weather_routing_engine::RoutingScenario* scenario,
    const wxString& status,
    const wxString& failureReason,
    const std::vector<RouteMapOverlay*>& routes,
    wxString& error) {
  if (outputPath.IsEmpty()) return true;

  weather_routing_engine::RoutingResult result;
  result.scenario = scenario ? scenario->name : "Weather Routing headless run";
  result.status = status;
  result.failureReason = failureReason;
  for (auto route : routes) result.candidates.push_back(CandidateFromRoute(route));
  return SaveRoutingResultJson(outputPath, result, error);
}

}  // namespace weather_routing_headless
