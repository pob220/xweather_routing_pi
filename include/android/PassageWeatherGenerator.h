// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from xGRIB 0.4.5 Android touch generator, source a5606ce.
#pragma once
#include <functional>
#include <memory>
#include <wx/dialog.h>
#include "ocpn_plugin.h"
#include "PassageWeatherRequest.h"
#include "WorldForecastDialog.h"

int WR_ChooseForecastTime(wxWindow* parent, const wxArrayString& times, int selected);
void WR_FitForecastDialog(wxWindow* parent, wxDialog& dialog);

class AndroidPassageWeatherDialog : public wxDialog {
 public:
  using GribReadyCallback = std::function<void(const wxString&)>;
  AndroidPassageWeatherDialog(wxWindow* parent, GribReadyCallback ready);
  ~AndroidPassageWeatherDialog() override;
  void ShowMobile(const PlugIn_ViewPort& viewport);
  std::optional<weather_routing::WorldForecastDownload> ShowWorld(
      const PlugIn_ViewPort& viewport,
      const std::optional<weather_routing::PassageForecastDefaults>& passage);
  void UseDownloadedCurrents(const weather_routing::WorldForecastDownload& current,
      const wxString& weather,
      const std::optional<weather_routing::PassageForecastDefaults>& passage);
 private:
  void ShowMode(const PlugIn_ViewPort& viewport, bool serverDownload);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
