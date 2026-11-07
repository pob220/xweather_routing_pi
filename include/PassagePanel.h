// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include "Boat.h"
class WeatherRouting;
class EnvironmentalGribDialog;
class wxChoice;
class wxStaticText;
class wxButton;

class PassagePanel : public wxScrolledWindow {
public:
  PassagePanel(wxWindow* parent, WeatherRouting& routing);
  ~PassagePanel() override;
  void PrepareForShutdown();
  void RefreshPassage();

private:
  void EnsurePassage();
  void SelectPassage(int index);
  void EditRoute();
  void ChooseBoat();
  void ShowLibrary();
  void LoadWeather();
  void GenerateWeather();
  void ApplyBoat(const wxString& path);
  WeatherRouting& routing_;
  wxChoice* passages_;
  wxStaticText *route_, *boat_, *weather_, *limits_, *status_;
  wxButton *calculate_, *edit_, *library_, *load_;
  wxTimer refresh_;
  Boat boatModel_;
  EnvironmentalGribDialog* generator_{nullptr};
};
