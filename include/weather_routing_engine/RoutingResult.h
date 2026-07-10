/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ENGINE_ROUTING_RESULT_H_
#define _WEATHER_ROUTING_ENGINE_ROUTING_RESULT_H_

#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

#include "weather_routing_engine/RoutingDiagnostics.h"

namespace weather_routing_engine {

struct RoutingCandidateResult {
  wxDateTime departure;
  wxString state;
  wxDateTime eta;
  long elapsedSeconds;
  double distanceNm;
  wxString finalSafety;
  wxString failureReason;
  int offsetMinutes;
  bool reverseRecoveryUsed;
  wxString reverseRecoveryStatus;
  long reverseLayersBuilt;
  long reverseNodesGenerated;
  long reverseNodesFeasible;
  bool reverseConnectionFound;
  wxDateTime reverseConnectionTime;
  wxString reverseFailureReason;
  bool reverseFinalValidationPass;

  RoutingCandidateResult()
      : elapsedSeconds(-1),
        distanceNm(-1.0),
        offsetMinutes(0),
        reverseRecoveryUsed(false),
        reverseLayersBuilt(-1),
        reverseNodesGenerated(-1),
        reverseNodesFeasible(-1),
        reverseConnectionFound(false),
        reverseFinalValidationPass(false) {}
};

struct RoutingResult {
  int schemaVersion;
  wxString scenario;
  wxString status;
  wxString failureReason;
  std::vector<RoutingCandidateResult> candidates;
  RoutingDiagnostics diagnostics;

  RoutingResult() : schemaVersion(1), status("unknown") {}
};

}  // namespace weather_routing_engine

#endif
