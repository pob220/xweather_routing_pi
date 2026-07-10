/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ENGINE_ROUTING_SCENARIO_H_
#define _WEATHER_ROUTING_ENGINE_ROUTING_SCENARIO_H_

#include <wx/datetime.h>
#include <wx/string.h>

namespace weather_routing_engine {

struct RoutingScenarioPosition {
  wxString name;
  double lat;
  double lon;

  RoutingScenarioPosition() : lat(0.0), lon(0.0) {}
};

struct RoutingScenarioDepartureOptimization {
  bool enabled;
  int beforeMinutes;
  int afterMinutes;
  int stepMinutes;

  RoutingScenarioDepartureOptimization()
      : enabled(false), beforeMinutes(0), afterMinutes(0), stepMinutes(60) {}
};

struct RoutingScenarioEnvironment {
  bool useCurrents;
  bool hasUseCurrents;

  RoutingScenarioEnvironment() : useCurrents(false), hasUseCurrents(false) {}
};

struct RoutingScenarioSafety {
  wxString mode;  // none, gshhs, chart
  bool enforce;
  bool hasEnforce;
  double landMarginNm;
  bool hasLandMarginNm;
  double minimumDepthM;
  bool hasMinimumDepthM;
  bool persistentCertifiedCacheEnabled;
  bool hasPersistentCertifiedCacheEnabled;

  RoutingScenarioSafety()
      : mode(""),
        enforce(false),
        hasEnforce(false),
        landMarginNm(0.0),
        hasLandMarginNm(false),
        minimumDepthM(0.0),
        hasMinimumDepthM(false),
        persistentCertifiedCacheEnabled(false),
        hasPersistentCertifiedCacheEnabled(false) {}
};

struct RoutingScenarioReverseReachability {
  bool enabled;
  int searchBackIsochrones;
  bool hasSearchBackIsochrones;
  double horizonHours;
  bool hasHorizonHours;
  bool diagnostics;
  bool hasDiagnostics;

  RoutingScenarioReverseReachability()
      : enabled(false),
        searchBackIsochrones(6),
        hasSearchBackIsochrones(false),
        horizonHours(0.0),
        hasHorizonHours(false),
        diagnostics(false),
        hasDiagnostics(false) {}
};

struct RoutingScenario {
  int schemaVersion;
  wxString name;
  RoutingScenarioPosition start;
  RoutingScenarioPosition end;
  wxDateTime startTime;
  RoutingScenarioDepartureOptimization departureOptimization;
  RoutingScenarioEnvironment environment;
  RoutingScenarioSafety safety;
  RoutingScenarioReverseReachability reverseReachability;

  RoutingScenario() : schemaVersion(1) {}
};

}  // namespace weather_routing_engine

#endif
