/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2015 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */

#include <wx/wx.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "tinyxml.h"

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "ConfigurationDialog.h"
#include "BoatDialog.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"
#include "icons.h"

#include <algorithm>

namespace {

bool GetWaypointByGuid(const wxString& guid, PlugIn_Waypoint* waypoint) {
  return !guid.IsEmpty() && GetSingleWaypoint(guid, waypoint);
}

bool FindWaypointByName(const wxString& name, PlugIn_Waypoint* waypoint,
                        wxString* guid = nullptr) {
  wxArrayString waypoint_guids = GetWaypointGUIDArray();
  for (const auto& waypoint_guid : waypoint_guids) {
    PlugIn_Waypoint candidate;
    if (!GetSingleWaypoint(waypoint_guid, &candidate)) continue;
    if (candidate.m_MarkName != name) continue;

    if (waypoint) *waypoint = candidate;
    if (guid) *guid = waypoint_guid;
    return true;
  }
  return false;
}

wxString WaypointNameForGuid(const wxString& guid) {
  PlugIn_Waypoint waypoint;
  if (GetWaypointByGuid(guid, &waypoint)) return waypoint.m_MarkName;
  return wxEmptyString;
}

wxString GetWaypointGuidForSelection(wxComboBox* combo) {
  if (!combo) return wxEmptyString;

  int selection = combo->GetSelection();
  wxArrayString waypoint_guids = GetWaypointGUIDArray();
  if (selection >= 0 && selection < (int)waypoint_guids.GetCount())
    return waypoint_guids[selection];

  wxString guid;
  FindWaypointByName(combo->GetValue(), nullptr, &guid);
  return guid;
}

}  // namespace

ConfigurationDialog::ConfigurationDialog(WeatherRouting& weatherrouting)
#ifndef __WXOSX__
    : ConfigurationDialogBase(&weatherrouting),
#else
    : ConfigurationDialogBase(&weatherrouting, wxID_ANY,
                              _("Weather Routing Configuration"),
                              wxDefaultPosition, wxDefaultSize,
                              wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(weatherrouting),
      m_bBlockUpdate(false) {
  const wxString detect_land_note =
      _("Detect Land uses OpenCPN chart land checks where vector or CM93 "
        "chart geometry is available, with GSHHS background shoreline data "
        "as a fallback. Accuracy depends on chart coverage and installed "
        "GSHHS quality.");
  m_cbDetectLand->SetToolTip(detect_land_note);
  m_sSafetyMarginLand->SetToolTip(
      detect_land_note + _("\n\nSpecify a minimum distance in nautical miles "
                           "to maintain from land during routing "
                           "calculations."));
  m_cbUseExperimentalChartSafety->SetToolTip(
      _("Use OpenCPN chart-backed land checks for Detect Land diagnostics. "
        "Route rejection uses these checks when chart-aware land avoidance is "
        "enabled."));
  m_cbEnforceExperimentalChartSafety->SetToolTip(
      _("Allow chart-backed land checks to reject route candidates and fail "
        "completed routes whose final track crosses chart land."));

  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  m_cbUseExperimentalChartSafety->SetValue(
      (bool)pConf->Read(_T("UseExperimentalChartSafety"), 0L));
  m_cbEnforceExperimentalChartSafety->SetValue(
      (bool)pConf->Read(_T("EnforceExperimentalChartSafety"), 0L));

#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#else
  wxPoint p = GetPosition();
  pConf->Read(_T ( "ConfigurationX" ), &p.x, p.x);
  pConf->Read(_T ( "ConfigurationY" ), &p.y, p.y);
  SetPosition(p);
#endif
}

ConfigurationDialog::~ConfigurationDialog() {
  if (getenv("WR_HEADLESS_ROUTE_TEST")) {
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST shutdown: skipping ConfigurationDialog "
        "position save during headless app teardown.");
    return;
  }
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  wxPoint p = GetPosition();
  pConf->Write(_T ( "ConfigurationX" ), p.x);
  pConf->Write(_T ( "ConfigurationY" ), p.y);
}

void ConfigurationDialog::EditBoat() {
  m_WeatherRouting.m_BoatDialog.LoadPolar(m_tBoat->GetValue());
  m_WeatherRouting.m_BoatDialog.Show();
}
void ConfigurationDialog::OnGribTime(wxCommandEvent& event) {
  SetStartDateTime(m_GribTimelineTime);
  Update();
}

void ConfigurationDialog::OnCurrentTime(wxCommandEvent& event) {
  SetStartDateTime(wxDateTime::Now());
  Update();
}

void ConfigurationDialog::OnUseCurrentTime(wxCommandEvent& event) {
  m_dpStartDate->Enable(!m_cbUseCurrentTime->IsChecked());
  m_tpTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_bGribTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_bCurrentTime->Enable(!m_cbUseCurrentTime->IsChecked());
  Update();
}

void ConfigurationDialog::OnStartFromBoat(wxCommandEvent& event) {
  m_cStart->Enable(!m_rbStartFromBoat->GetValue());
  Update();
}

void ConfigurationDialog::OnStartFromPosition(wxCommandEvent& event) {
  AddPositions(true);
  m_cStart->Enable(m_rbStartPositionSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnStartFromWaypoint(wxCommandEvent& event) {
  AddWaypoints(true);
  m_cStart->Enable(m_rbStartWaypointSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnEndAtPosition(wxCommandEvent& event) {
  AddPositions(false);
  m_cEnd->Enable(m_rbEndPositionSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnEndAtWaypoint(wxCommandEvent& event) {
  AddWaypoints(false);
  m_cEnd->Enable(m_rbEndWaypointSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnAvoidCyclones(wxCommandEvent& event) { Update(); }

void ConfigurationDialog::OnUseMotor(wxCommandEvent& event) {
  const bool enabled = m_cbUseMotor->IsChecked();
  m_sMotorSpeedThreshold->Enable(enabled);
  m_sMotorSpeed->Enable(enabled);
  OnValueChange(event);
  Update();
}

void ConfigurationDialog::OnUseOptimalAngles(wxCommandEvent& event) {
  // Optimal angles refine the user's configured course envelope; they never
  // widen or overwrite it.
  Update();
}

void ConfigurationDialog::OnBoatFilename(wxCommandEvent& event) {
  wxFileDialog openDialog(
      this, _("Select Boat File"), wxFileName(m_tBoat->GetValue()).GetPath(),
      wxT(""), wxT("xml (*.xml)|*.XML;*.xml|All files (*.*)|*.*"), wxFD_OPEN);

  if (openDialog.ShowModal() == wxID_OK) SetBoatFilename(openDialog.GetPath());
}

#define SET_CHECKBOX_FIELD(FIELD, VALUE)                          \
  do {                                                            \
    bool alltrue = true, allfalse = true;                         \
    for (std::list<RouteMapConfiguration>::iterator it =          \
             configurations.begin();                              \
         it != configurations.end(); it++)                        \
      if (VALUE)                                                  \
        allfalse = false;                                         \
      else                                                        \
        alltrue = false;                                          \
    m_cb##FIELD->Set3StateValue(alltrue    ? wxCHK_CHECKED        \
                                : allfalse ? wxCHK_UNCHECKED      \
                                           : wxCHK_UNDETERMINED); \
  } while (0)

#define SET_CHECKBOX(FIELD) SET_CHECKBOX_FIELD(FIELD, (*it).FIELD)

#define SET_CONTROL_VALUE(VALUE, CONTROL, SETTER, TYPE, NULLVALUE)          \
  do {                                                                      \
    bool allsame = true;                                                    \
    std::list<RouteMapConfiguration>::iterator it = configurations.begin(); \
    TYPE value = VALUE;                                                     \
    for (it++; it != configurations.end(); it++) {                          \
      if (value != VALUE) {                                                 \
        allsame = false;                                                    \
        break;                                                              \
      }                                                                     \
    }                                                                       \
    CONTROL->SETTER(allsame ? value : NULLVALUE);                           \
    wxSize s(CONTROL->GetSize());                                           \
    if (allsame)                                                            \
      CONTROL->SetForegroundColour(wxColour(0, 0, 0));                      \
    else                                                                    \
      CONTROL->SetForegroundColour(wxColour(180, 180, 180));                \
    CONTROL->Fit();                                                         \
    CONTROL->SetSize(s);                                                    \
  } while (0)

#define SET_CONTROL(FIELD, CONTROL, SETTER, TYPE, NULLVALUE) \
  SET_CONTROL_VALUE((*it).FIELD, CONTROL, SETTER, TYPE, NULLVALUE)

#define SET_CHOICE_VALUE(FIELD, VALUE)                                        \
  do {                                                                        \
    bool allsame = true;                                                      \
    std::list<RouteMapConfiguration>::iterator it = configurations.begin();   \
    wxString value = VALUE;                                                   \
    for (it++; it != configurations.end(); it++) {                            \
      if (value != VALUE) {                                                   \
        allsame = false;                                                      \
        break;                                                                \
      }                                                                       \
    }                                                                         \
    if (allsame)                                                              \
      m_c##FIELD->SetValue(value);                                            \
    else {                                                                    \
      if (m_c##FIELD->GetString(m_c##FIELD->GetCount() - 1) != wxEmptyString) \
        m_c##FIELD->Append(wxEmptyString);                                    \
      m_c##FIELD->SetValue(wxEmptyString);                                    \
    }                                                                         \
  } while (0)
#define SET_CHOICE(FIELD) SET_CHOICE_VALUE(FIELD, (*it).FIELD)

#define SET_SPIN_VALUE(FIELD, VALUE) \
  SET_CONTROL_VALUE(VALUE, m_s##FIELD, SetValue, int, value)

#define SET_SPIN(FIELD) SET_SPIN_VALUE(FIELD, (*it).FIELD)

#define SET_SPIN_DOUBLE_VALUE(FIELD, VALUE) \
  SET_CONTROL_VALUE(VALUE, m_s##FIELD, SetValue, double, value)

#define SET_SPIN_DOUBLE(FIELD) SET_SPIN_DOUBLE_VALUE(FIELD, (*it).FIELD)

#ifdef __OCPN__ANDROID__
#define NO_EDITED_CONTROLS 1
#else
#define NO_EDITED_CONTROLS 0
#endif

void ConfigurationDialog::SetConfigurations(
    std::list<RouteMapConfiguration> configurations) {
  m_bBlockUpdate = true;

  m_edited_controls.clear();

  if (configurations.empty()) {
    m_bBlockUpdate = false;
    return;
  }

  std::list<RouteMapConfiguration>::iterator it = configurations.begin();

  const bool useLocalTime =
      m_WeatherRouting.m_SettingsDialog.m_cbUseLocalTime->IsChecked();
  wxDateTime dateValue = it->StartTime.GetDateOnly();
  wxDateTime timeValue = it->StartTime;
  // The native picker controls interpret their wxDateTime input as UTC and
  // convert it for display. Undo that conversion when the UI is showing UTC.
  if (!useLocalTime) {
    dateValue = dateValue.ToUTC();
    timeValue = timeValue.ToUTC();
  }
  SET_CONTROL_VALUE(dateValue, m_dpStartDate, SetValue, wxDateTime,
                    wxDateTime());
  SET_CONTROL_VALUE(timeValue, m_tpTime, SetValue, wxDateTime, wxDateTime());

  SET_CHECKBOX(UseCurrentTime);
  SET_CHECKBOX(DepartureTimeOptimizationEnabled);
  SET_SPIN_VALUE(DepartureTimeOptimizationRangeHours,
                 (*it).DepartureTimeOptimizationRangeMinutes / 60);
  SET_SPIN_VALUE(DepartureTimeOptimizationStepHours,
                 (*it).DepartureTimeOptimizationStepMinutes / 60);
  SET_SPIN_VALUE(DepartureTimeOptimizationStepMinutes,
                 (*it).DepartureTimeOptimizationStepMinutes % 60);
  SET_SPIN(DepartureTimeOptimizationConcurrentRoutes);
  m_sChartSafetyRamCacheMiB->SetValue(
      m_WeatherRouting.ChartSafetyRamCacheMiB());
  UpdateChartSafetyRamLabel();

  bool timeButtonsEnabled = m_tpTime->IsEnabled() &&
                            m_dpStartDate->IsEnabled() &&
                            !m_cbUseCurrentTime->IsChecked();
  m_dpStartDate->Enable(timeButtonsEnabled);
  m_tpTime->Enable(timeButtonsEnabled);
  m_bCurrentTime->Enable(timeButtonsEnabled);
  m_bGribTime->Enable(timeButtonsEnabled);

  SET_SPIN_VALUE(TimeStepHours, (int)((*it).DeltaTime / 3600));
  SET_SPIN_VALUE(TimeStepMinutes, ((int)(*it).DeltaTime / 60) % 60);

  SET_CONTROL(boatFileName, m_tBoat, SetValue, wxString, _T(""));
  long l = m_tBoat->GetValue().Length();
  m_tBoat->SetSelection(l, l);

  // if there's a Route GUID it's an OpenCPN route, in that case disable start
  // and end.
  bool oRoute = false;
  bool allStartFromBoat = true;
  bool allStartFromPosition = true;
  bool allStartFromWaypoint = true;
  bool allEndAtPosition = true;
  bool allEndAtWaypoint = true;
  for (auto it : configurations) {
    if (!it.RouteGUID.IsEmpty()) {
      oRoute = true;
      break;
    }
    if (it.StartType != RouteMapConfiguration::START_FROM_BOAT)
      allStartFromBoat = false;
    if (it.StartType != RouteMapConfiguration::START_FROM_POSITION)
      allStartFromPosition = false;
    if (it.StartType != RouteMapConfiguration::START_FROM_WAYPOINT)
      allStartFromWaypoint = false;
    if (it.EndType != RouteMapConfiguration::END_AT_POSITION)
      allEndAtPosition = false;
    if (it.EndType != RouteMapConfiguration::END_AT_WAYPOINT)
      allEndAtWaypoint = false;
  }

  if (allStartFromWaypoint)
    AddWaypoints(true);
  else
    AddPositions(true);
  if (allEndAtWaypoint)
    AddWaypoints(false);
  else
    AddPositions(false);

  wxString start = (*it).Start;
  if (allStartFromWaypoint && !(*it).StartGUID.IsEmpty()) {
    wxString waypoint_name = WaypointNameForGuid((*it).StartGUID);
    if (!waypoint_name.IsEmpty()) start = waypoint_name;
  }
  SET_CHOICE_VALUE(Start, start);

  wxString end = (*it).End;
  if (allEndAtWaypoint && !(*it).EndGUID.IsEmpty()) {
    wxString waypoint_name = WaypointNameForGuid((*it).EndGUID);
    if (!waypoint_name.IsEmpty()) end = waypoint_name;
  }
  SET_CHOICE_VALUE(End, end);

  m_rbStartFromBoat->Enable(!oRoute);
  m_rbStartPositionSelection->Enable(!oRoute);
  m_rbStartWaypointSelection->Enable(!oRoute);
  m_rbEndPositionSelection->Enable(!oRoute);
  m_rbEndWaypointSelection->Enable(!oRoute);
  m_rbStartFromBoat->SetValue(allStartFromBoat);
  m_rbStartPositionSelection->SetValue(allStartFromPosition);
  m_rbStartWaypointSelection->SetValue(allStartFromWaypoint);
  m_rbEndPositionSelection->SetValue(allEndAtPosition);
  m_rbEndWaypointSelection->SetValue(allEndAtWaypoint);

  m_cStart->Enable(!oRoute && !m_rbStartFromBoat->GetValue());
  m_cEnd->Enable(!oRoute);

  m_bGribTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_bCurrentTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_dpStartDate->Enable(!m_cbUseCurrentTime->IsChecked());
  m_tpTime->Enable(!m_cbUseCurrentTime->IsChecked());

  SET_SPIN(FromDegree);
  SET_SPIN(ToDegree);
  SET_CHECKBOX(UseOptimalAngles);
  SET_SPIN_DOUBLE(ByDegrees);

  SET_CHECKBOX(UseMotor);
  SET_SPIN_DOUBLE(MotorSpeedThreshold);
  SET_SPIN_DOUBLE(MotorSpeed);
  const bool motorEnabled = m_cbUseMotor->IsChecked();
  m_sMotorSpeedThreshold->Enable(motorEnabled);
  m_sMotorSpeed->Enable(motorEnabled);

  SET_CHOICE_VALUE(Integrator,
                   ((*it).Integrator == RouteMapConfiguration::RUNGE_KUTTA
                        ? _T("Runge Kutta")
                        : _T("Newton")));

  SET_SPIN(MaxDivertedCourse);
  SET_SPIN(MaxCourseAngle);
  SET_SPIN(MaxSearchAngle);
  SET_SPIN(MaxTrueWindKnots);
  SET_SPIN(MaxApparentWindKnots);

  SET_SPIN_DOUBLE(MaxSwellMeters);
  SET_SPIN(MaxLatitude);
  SET_SPIN(TackingTime);
  SET_SPIN(JibingTime);
  SET_SPIN(SailPlanChangeTime);
  SET_SPIN(WindVSCurrent);

  SET_CHECKBOX(AvoidCycloneTracks);
  SET_SPIN(CycloneMonths);
  SET_SPIN(CycloneDays);
  SET_SPIN_DOUBLE(SafetyMarginLand);

  SET_CHECKBOX(DetectLand);
  SET_CHECKBOX(DetectBoundary);
  SET_CHECKBOX(Currents);
  SET_CHECKBOX(OptimizeTacking);

  SET_CHECKBOX(InvertedRegions);
  SET_CHECKBOX(UseReverseReachabilityRecovery);
  SET_CHECKBOX(Anchoring);

  SET_CHECKBOX(UseGrib);
  SET_CONTROL(ClimatologyType, m_cClimatologyType, SetSelection, int, -1);
  SET_CHECKBOX(AllowDataDeficient);
  SET_SPIN_VALUE(WindStrength, (int)((*it).WindStrength * 100));

  SET_SPIN_VALUE(UpwindEfficiency, (int)((*it).UpwindEfficiency * 100));
  SET_SPIN_VALUE(DownwindEfficiency, (int)((*it).DownwindEfficiency * 100));
  SET_SPIN_VALUE(NightCumulativeEfficiency,
                 (int)((*it).NightCumulativeEfficiency * 100));

  m_bBlockUpdate = false;
}

void ConfigurationDialog::AddSource(wxString name) {
  if (m_rbStartPositionSelection->GetValue()) m_cStart->Append(name);
  if (m_rbEndPositionSelection->GetValue()) m_cEnd->Append(name);
}

void ConfigurationDialog::RemoveSource(wxString name) {
  int i = m_cStart->FindString(name, true);
  if (i >= 0) m_cStart->Delete(i);
  i = m_cEnd->FindString(name, true);
  if (i >= 0) m_cEnd->Delete(i);
}

void ConfigurationDialog::RenameSource(const wxString& oldName,
                                       const wxString& newName) {
  int i = m_cStart->FindString(oldName, true);
  if (i >= 0) m_cStart->SetString(i, newName);
  i = m_cEnd->FindString(oldName, true);
  if (i >= 0) m_cEnd->SetString(i, newName);
}

void ConfigurationDialog::ClearSources() {
  m_cStart->Clear();
  m_cEnd->Clear();
}

void ConfigurationDialog::AddWaypoints(const bool toStart) {
  wxComboBox* combo = toStart ? m_cStart : m_cEnd;
  wxString value = combo->GetValue();
  combo->Clear();

  wxArrayString waypoint_guids = GetWaypointGUIDArray();
  for (const auto& guid : waypoint_guids) {
    PlugIn_Waypoint waypoint;
    if (GetSingleWaypoint(guid, &waypoint))
      combo->Append(waypoint.m_MarkName);
  }

  if (!value.IsEmpty()) combo->SetValue(value);
}

void ConfigurationDialog::AddPositions(const bool toStart) {
  wxComboBox* combo = toStart ? m_cStart : m_cEnd;
  wxString value = combo->GetValue();
  combo->Clear();

  for (const auto& position : RouteMap::Positions)
    combo->Append(position.Name);

  if (!value.IsEmpty()) combo->SetValue(value);
}

void ConfigurationDialog::SetBoatFilename(wxString path) {
  m_tBoat->SetValue(path);
  long l = m_tBoat->GetValue().Length();
  m_tBoat->SetSelection(l, l);

  Update();
}

void ConfigurationDialog::OnResetAdvanced(wxCommandEvent& event) {
  m_bBlockUpdate = true;

  // constraints
  m_sMaxLatitude->SetValue(90);
  m_sWindVSCurrent->SetValue(0);
  m_sMaxCourseAngle->SetValue(180);
  m_sMaxSearchAngle->SetValue(120);
  m_cbAvoidCycloneTracks->SetValue(false);
  // XXX missing 2

  // Options
  m_cbInvertedRegions->SetValue(false);
  m_cbUseReverseReachabilityRecovery->SetValue(false);
  m_cbAnchoring->SetValue(false);
  m_sDepartureTimeOptimizationConcurrentRoutes->SetValue(0);
  m_edited_controls.push_back(
      m_sDepartureTimeOptimizationConcurrentRoutes);
  m_sChartSafetyRamCacheMiB->SetValue(0);
  m_edited_controls.push_back(m_sChartSafetyRamCacheMiB);
  m_cIntegrator->SetSelection(0);
  m_sWindStrength->SetValue(100);
  m_sUpwindEfficiency->SetValue(100);
  m_sDownwindEfficiency->SetValue(100);
  m_sNightCumulativeEfficiency->SetValue(100);
  m_sTackingTime->SetValue(0);
  m_sJibingTime->SetValue(0);
  m_sSailPlanChangeTime->SetValue(0);
  m_sSafetyMarginLand->SetValue(0.);

  m_sFromDegree->SetValue(0);
  m_sToDegree->SetValue(180);
  m_cbUseOptimalAngles->SetValue(false);
  m_sByDegrees->SetValue(5.0);

  m_cbUseMotor->SetValue(false);
  m_sMotorSpeedThreshold->SetValue(2.0);
  m_sMotorSpeed->SetValue(5.0);
  m_sMotorSpeedThreshold->Enable(false);
  m_sMotorSpeed->Enable(false);

  m_bBlockUpdate = false;
  Update();
}

void ConfigurationDialog::UpdateChartSafetyRamLabel() {
  const int effective = m_WeatherRouting.EffectiveChartSafetyRamCacheMiB();
  m_tChartSafetyRamEffective->SetLabel(
      m_sChartSafetyRamCacheMiB->GetValue() == 0
          ? wxString::Format(_("MiB; Auto = %d"), effective)
          : wxString::Format(_("MiB; active = %d"), effective));
}

void ConfigurationDialog::SetStartDateTime(wxDateTime datetime) {
  if (datetime.IsValid()) {
    if (!m_WeatherRouting.m_SettingsDialog.m_cbUseLocalTime->IsChecked())
      datetime = datetime.ToUTC();

    m_dpStartDate->SetValue(datetime);
    m_tpTime->SetValue(datetime);
    m_edited_controls.push_back(m_tpTime);
    m_edited_controls.push_back(m_dpStartDate);
  } else {
    wxMessageDialog mdlg(this, _("Invalid Date Time."),
                         wxString(_("Weather Routing"), wxOK | wxICON_WARNING));
    mdlg.ShowModal();
  }
}

#define GET_CHECKBOX(FIELD)                                  \
  do {                                                       \
    if (m_cb##FIELD->Get3StateValue() == wxCHK_UNCHECKED)    \
      configuration.FIELD = false;                           \
    else if (m_cb##FIELD->Get3StateValue() == wxCHK_CHECKED) \
      configuration.FIELD = true;                            \
  } while (0)

#define GET_SPIN(FIELD)                                              \
  if (NO_EDITED_CONTROLS ||                                          \
      std::find(m_edited_controls.begin(), m_edited_controls.end(),  \
                (wxObject*)m_s##FIELD) != m_edited_controls.end()) { \
    configuration.FIELD = m_s##FIELD->GetValue();                    \
    m_s##FIELD->SetForegroundColour(wxColour(0, 0, 0));              \
  }

#define GET_CHOICE(FIELD)                                                     \
  if (NO_EDITED_CONTROLS ||                                                   \
      std::find(m_edited_controls.begin(), m_edited_controls.end(),           \
                (wxObject*)m_c##FIELD) != m_edited_controls.end())            \
    if (m_c##FIELD->GetValue() != wxEmptyString) {                            \
      configuration.FIELD = m_c##FIELD->GetValue();                           \
      if (m_c##FIELD->GetString(m_c##FIELD->GetCount() - 1) == wxEmptyString) \
        m_c##FIELD->Delete(m_c##FIELD->GetCount() - 1);                       \
    }

void ConfigurationDialog::Update() {
  if (m_bBlockUpdate) return;

  if (std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_sChartSafetyRamCacheMiB) !=
      m_edited_controls.end()) {
    m_WeatherRouting.SetChartSafetyRamCacheMiB(
        m_sChartSafetyRamCacheMiB->GetValue());
    m_sChartSafetyRamCacheMiB->SetForegroundColour(wxColour(0, 0, 0));
    UpdateChartSafetyRamLabel();
  }

  m_cStart->Enable(!m_rbStartFromBoat->GetValue());
  m_cEnd->Enable(true);

  bool refresh = false;
  RouteMapConfiguration configuration;
  std::list<RouteMapOverlay*> currentroutemaps =
      m_WeatherRouting.CurrentRouteMaps();
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    configuration = (*it)->GetConfiguration();

    // Set the start type based on the radio button selection
    if (m_rbStartFromBoat->GetValue()) {
      configuration.StartType = RouteMapConfiguration::START_FROM_BOAT;
      configuration.StartGUID = wxEmptyString;
    } else if (m_rbStartWaypointSelection->GetValue()) {
      configuration.StartType = RouteMapConfiguration::START_FROM_WAYPOINT;
      GET_CHOICE(Start);
      configuration.StartGUID = GetWaypointGuidForSelection(m_cStart);
    } else {
      configuration.StartType = RouteMapConfiguration::START_FROM_POSITION;
      GET_CHOICE(Start);
      configuration.StartGUID = wxEmptyString;
    }

    if (m_rbEndWaypointSelection->GetValue()) {
      configuration.EndType = RouteMapConfiguration::END_AT_WAYPOINT;
      GET_CHOICE(End);
      configuration.EndGUID = GetWaypointGuidForSelection(m_cEnd);
    } else {
      configuration.EndType = RouteMapConfiguration::END_AT_POSITION;
      GET_CHOICE(End);
      configuration.EndGUID = wxEmptyString;
    }

    GET_CHECKBOX(UseCurrentTime);
    GET_CHECKBOX(DepartureTimeOptimizationEnabled);
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sDepartureTimeOptimizationRangeHours) !=
            m_edited_controls.end()) {
      configuration.DepartureTimeOptimizationRangeMinutes =
          60 * m_sDepartureTimeOptimizationRangeHours->GetValue();
      m_sDepartureTimeOptimizationRangeHours->SetForegroundColour(
          wxColour(0, 0, 0));
    }
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sDepartureTimeOptimizationStepHours) !=
            m_edited_controls.end() ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sDepartureTimeOptimizationStepMinutes) !=
            m_edited_controls.end()) {
      configuration.DepartureTimeOptimizationStepMinutes =
          60 * m_sDepartureTimeOptimizationStepHours->GetValue() +
          m_sDepartureTimeOptimizationStepMinutes->GetValue();
      m_sDepartureTimeOptimizationStepHours->SetForegroundColour(
          wxColour(0, 0, 0));
      m_sDepartureTimeOptimizationStepMinutes->SetForegroundColour(
          wxColour(0, 0, 0));
    }

    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_dpStartDate) != m_edited_controls.end() ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_tpTime) != m_edited_controls.end()) {
      if (!m_dpStartDate->GetDateCtrlValue().IsValid()) continue;

      wxDateTime controlDate = m_dpStartDate->GetDateCtrlValue();
      wxDateTime controlTime = m_tpTime->GetTimeCtrlValue();
      wxDateTime time(controlDate.GetDay(), controlDate.GetMonth(),
                      controlDate.GetYear(), controlTime.GetHour(),
                      controlTime.GetMinute(), controlTime.GetSecond());
      if (!time.IsValid())
        time = wxDateTime::Now();
      else if (!m_WeatherRouting.m_SettingsDialog.m_cbUseLocalTime->IsChecked())
        time = time.FromUTC();

      configuration.StartTime = time;
      if (std::find(m_edited_controls.begin(), m_edited_controls.end(),
                    (wxObject*)m_dpStartDate) != m_edited_controls.end())
        m_dpStartDate->SetForegroundColour(wxColour(0, 0, 0));
      if (std::find(m_edited_controls.begin(), m_edited_controls.end(),
                    (wxObject*)m_tpTime) != m_edited_controls.end())
        m_tpTime->SetForegroundColour(wxColour(0, 0, 0));
    }

    if (!m_tBoat->GetValue().empty()) {
      configuration.boatFileName = m_tBoat->GetValue();
      m_tBoat->SetForegroundColour(wxColour(0, 0, 0));
    }

    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sTimeStepHours) != m_edited_controls.end() ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sTimeStepMinutes) != m_edited_controls.end()) {
      configuration.DeltaTime = 60 * (60 * m_sTimeStepHours->GetValue() +
                                      m_sTimeStepMinutes->GetValue());
      m_sTimeStepHours->SetForegroundColour(wxColour(0, 0, 0));
      m_sTimeStepMinutes->SetForegroundColour(wxColour(0, 0, 0));
    }

    if (m_cIntegrator->GetValue() == _T("Newton"))
      configuration.Integrator = RouteMapConfiguration::NEWTON;
    else if (m_cIntegrator->GetValue() == _T("Runge Kutta"))
      configuration.Integrator = RouteMapConfiguration::RUNGE_KUTTA;

    GET_SPIN(MaxDivertedCourse);
    GET_SPIN(MaxCourseAngle);
    GET_SPIN(MaxSearchAngle);
    GET_SPIN(MaxTrueWindKnots);
    GET_SPIN(MaxApparentWindKnots);

    GET_SPIN(MaxSwellMeters);
    GET_SPIN(MaxLatitude);
    GET_SPIN(TackingTime);
    GET_SPIN(JibingTime);
    GET_SPIN(SailPlanChangeTime);
    GET_SPIN(WindVSCurrent);

    if (m_sWindStrength->IsEnabled())
      configuration.WindStrength = m_sWindStrength->GetValue() / 100.0;

    if (m_sUpwindEfficiency->IsEnabled())
      configuration.UpwindEfficiency = m_sUpwindEfficiency->GetValue() / 100.0;
    if (m_sDownwindEfficiency->IsEnabled())
      configuration.DownwindEfficiency =
          m_sDownwindEfficiency->GetValue() / 100.0;
    if (m_sNightCumulativeEfficiency->IsEnabled())
      configuration.NightCumulativeEfficiency =
          m_sNightCumulativeEfficiency->GetValue() / 100.0;

    GET_CHECKBOX(AvoidCycloneTracks);
    GET_SPIN(CycloneMonths);
    GET_SPIN(CycloneDays);
    GET_SPIN(SafetyMarginLand);

    GET_CHECKBOX(DetectLand);
    GET_CHECKBOX(DetectBoundary);
    GET_CHECKBOX(Currents);
    GET_CHECKBOX(OptimizeTacking);

    GET_CHECKBOX(InvertedRegions);
    GET_CHECKBOX(UseReverseReachabilityRecovery);
    GET_CHECKBOX(Anchoring);
    GET_SPIN(DepartureTimeOptimizationConcurrentRoutes);

    GET_CHECKBOX(UseGrib);
    if (m_cClimatologyType->GetSelection() != -1)
      configuration.ClimatologyType =
          (RouteMapConfiguration::ClimatologyDataType)
              m_cClimatologyType->GetSelection();
    GET_CHECKBOX(AllowDataDeficient);
    if (m_sWindStrength->IsEnabled())
      configuration.WindStrength = m_sWindStrength->GetValue() / 100.0;

    GET_SPIN(FromDegree);
    GET_SPIN(ToDegree);
    GET_CHECKBOX(UseOptimalAngles);
    GET_SPIN(ByDegrees);

    GET_CHECKBOX(UseMotor);
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sMotorSpeedThreshold) !=
            m_edited_controls.end()) {
      configuration.MotorSpeedThreshold =
          m_sMotorSpeedThreshold->GetValue();
    }
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sMotorSpeed) != m_edited_controls.end()) {
      configuration.MotorSpeed = m_sMotorSpeed->GetValue();
    }

    m_WeatherRouting.PreserveMultiLegLegFieldsForDialog(*it, configuration);
    (*it)->SetConfiguration(configuration);
    m_WeatherRouting.SaveLastUsedConfigurationDefaults(configuration);

    /* if the start position changed, we must reset the route */
    RouteMapConfiguration newc = (*it)->GetConfiguration();
    if (newc.StartLat != configuration.StartLat ||
        newc.StartLon != configuration.StartLon) {
      (*it)->Reset();
      refresh = true;
    } else if (newc.EndLat != configuration.EndLat ||
               newc.EndLon != configuration.EndLon)
      refresh = true;  // update drawing
  }

  double by = m_sByDegrees->GetValue();
  if (m_sToDegree->GetValue() - m_sFromDegree->GetValue() < 2 * by) {
    wxMessageDialog mdlg(
        this, _("Warning: less than 4 different degree steps specified\n"),
        wxString(_("Weather Routing"), wxOK | wxICON_WARNING));
    mdlg.ShowModal();
  }

  m_WeatherRouting.UpdateCurrentConfigurations();

  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  pConf->Write(_T("UseExperimentalChartSafety"),
               m_cbUseExperimentalChartSafety->GetValue());
  pConf->Write(_T("EnforceExperimentalChartSafety"),
               m_cbEnforceExperimentalChartSafety->GetValue());

  if (refresh) m_WeatherRouting.GetParent()->Refresh();

  // Schedule auto-save to persist any configuration changes
  m_WeatherRouting.ScheduleAutoSave();
}
