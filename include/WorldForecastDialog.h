// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "AreaPreset.h"

class wxWindow;

namespace weather_routing {

struct WorldForecastDownload {
  wxString path;
  bool currentsOnly = false;
  xgrib::AreaPreset area;
};

#ifndef __OCPN__ANDROID__
std::optional<WorldForecastDownload> ChooseWorldForecast(
    wxWindow* parent, const xgrib::AreaPreset& area, int hours,
    const std::optional<xgrib::AreaPreset>& chartArea);
#endif

}  // namespace weather_routing
