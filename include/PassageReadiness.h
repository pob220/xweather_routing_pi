// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "RouteDiagnostics.h"
#include <array>
#include <wx/datetime.h>

namespace weather_routing {
enum class PassageSection { Route, Boat, Weather, Limits };
enum class PassageAction { Route, Boat, Weather, Limits };
enum class PassageWindPolicy {
  Forecast,
  Climatology,
  ForecastWithClimatology,
  None
};

// Facts sampled by the host adapter. Evaluation has no UI, plugin callbacks or
// file I/O, and can be reused before calculation and by automated checks.
struct PassageInputs {
  bool positionsValid{false}, distinctPositions{false}, timeValid{false};
  bool routeSequenceValid{true};
  bool boatValid{false}, exampleBoat{false};
  wxString boatError, boatWarnings;
  PassageWindPolicy policy{PassageWindPolicy::Forecast};
  bool forecastLoaded{false}, departureWind{false}, destinationWind{false};
  bool coverageChecked{true};
  bool climatologyWind{false};
  bool wavesAvailable{false}, currentsAvailable{false};
  bool requireWaves{false}, useCurrents{false}, avoidLand{true};
  double windLimit{30}, apparentWindLimit{40}, waveLimit{3};
  wxDateTime estimatedFinish, forecastEnd;
};
struct PassageReadiness {
  std::array<RouteDiagnosticReport, 4> sections;
  bool CanCalculate() const;
  unsigned Warnings() const;
  RouteDiagnosticReport Report() const;
};

PassageReadiness EvaluatePassage(const PassageInputs& input);
wxString PassageSectionStatus(const RouteDiagnosticReport& report);
PassageAction RecoveryAction(const std::string& diagnosticCode);

// Visible initial operating limits and their units have one definition.
struct PassageDefaults {
  static constexpr double windKnots = 30;
  static constexpr double apparentWindKnots = 40;
  static constexpr double waveMeters = 3;
};
}  // namespace weather_routing
