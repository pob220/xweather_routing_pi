// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <wx/datetime.h>

namespace weather_routing {
struct PassageWeatherWindow {
  wxDateTime start;
  int hours;
};
// Download timestamps must match hourly forecast records. Expand the request
// around the passage window; never alter the passage's departure or arrival.
std::optional<PassageWeatherWindow> BuildPassageWeatherWindow(
    const wxDateTime& departure, int hours, int stepHours, bool waves);
bool IsHourlyWeatherTime(const wxDateTime& instant);
}  // namespace weather_routing
