// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassagePanel.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "weather_routing_pi.h"
#include "PassageWeather.h"
#include "BundledClimatology.h"
#include "EnvironmentalGribDialog.h"
#include "tinyxml.h"
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/menu.h>
#include <wx/textdlg.h>
#include "RouteWaypointExtractor.h"
#include <wx/srchctrl.h>
#include <wx/listbox.h>
#include <wx/statbox.h>
#include <wx/valnum.h>
#include <algorithm>
#include <cmath>

namespace {
wxStaticText* Text(wxWindow* p, wxSizer* s, const wxString& label,
                   bool bold = false) {
  auto* t = new wxStaticText(p, wxID_ANY, label);
  if (wxDynamicCast(p, wxDialog)) t->Wrap(560);
  if (bold) t->SetFont(t->GetFont().Bold());
  s->Add(t, 0, wxEXPAND | wxALL, 7);
  return t;
}
wxButton* Button(wxWindow* p, wxSizer* s, const wxString& label,
                 std::function<void()> fn) {
  auto* b = new wxButton(p, wxID_ANY, label);
  b->SetMinSize(wxSize(-1, 36));
  s->Add(b, 1, wxEXPAND | wxALL, 5);
  b->Bind(wxEVT_BUTTON, [fn](wxCommandEvent&) { fn(); });
  return b;
}
wxString DisplayName(wxString s) {
  s.Replace("_", " ");
  return s;
}
}  // namespace

PassagePanel::PassagePanel(wxWindow* parent, WeatherRouting& routing)
    : wxPanel(parent, wxID_ANY), routing_(routing), refresh_(this) {
  auto* outer = new wxBoxSizer(wxVERTICAL);
  content_ = new wxScrolledWindow(this, wxID_ANY);
  content_->SetMinSize(wxSize(0, 0));
  // Native controls can need more width with large fonts or long translations.
  // Keep them reachable by scrolling and keyboard focus in compact windows.
  content_->SetScrollRate(12, 14);
  auto* page = new wxBoxSizer(wxVERTICAL);
  auto* plans = new wxBoxSizer(wxHORIZONTAL);
  passages_ = new wxChoice(content_, wxID_ANY);
  passages_->SetMinSize(wxSize(160, -1));
  passages_->SetName(_("Selected passage"));
  plans->Add(passages_, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5);
  auto* add = new wxButton(content_, wxID_ANY, _("New passage"));
  plans->Add(add, 0, wxALL, 5);
  add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { NewPassage(); });
  auto* more = new wxButton(content_, wxID_ANY, _("Passage actions"));
  plans->Add(more, 0, wxALL, 5);
  more->Bind(wxEVT_BUTTON, [this, more](wxCommandEvent&) {
    wxMenu menu;
    const int example = wxWindow::NewControlId(),
              reset = wxWindow::NewControlId();
    menu.Append(example, _("Try an example passage"));
    menu.Append(reset, _("Restore passage preferences…"));
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { NewPassage(true); }, example);
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { RestoreDefaults(); }, reset);
    more->PopupMenu(&menu);
  });
  mutableControls_ = {add, more, passages_};
  passages_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    SelectPassage(passages_->GetSelection());
  });
  page->Add(plans, 0, wxEXPAND | wxALL, 5);
  auto card = [&](const wxString& label) {
    auto* box = new wxStaticBoxSizer(wxVERTICAL, content_, label);
    box->GetStaticBox()->SetFont(box->GetStaticBox()->GetFont().Bold());
    page->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    return box;
  };
  resultsBox_ = card(_("Passage result"));
  results_ = Text(content_, resultsBox_, "");
  resultsBox_->Show(false);
  auto* route = card(_("1. Route"));
  route_ = Text(content_, route, "");
  route_->SetName(_("Route status"));
  auto* routeActions = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(
      Button(content_, routeActions, _("Edit route"), [this] { EditRoute(); }));
  mutableControls_.push_back(Button(content_, routeActions,
                                    _("Use OpenCPN route"),
                                    [this] { ChooseRoute(); }));
  Button(content_, routeActions, _("Waypoints and chart"),
         [this] { ShowWaypoints(); });
  route->Add(routeActions, 0, wxEXPAND);
  auto* boat = card(_("2. Boat and polars"));
  boat_ = Text(content_, boat, "");
  boat_->SetName(_("Boat status"));
  auto* boatActions = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(Button(content_, boatActions,
                                    _("Boat and polar library"),
                                    [this] { ShowLibrary(); }));
  mutableControls_.push_back(Button(content_, boatActions,
                                    _("Import boat / polar"),
                                    [this] { ChooseBoat(); }));
  mutableControls_.push_back(
      Button(content_, boatActions, _("Edit polars"), [this] {
        EnsurePassage();
        routing_.m_ConfigurationDialog.EditBoat();
      }));
  mutableControls_.push_back(Button(content_, boatActions, _("Boat details"),
                                    [this] { EditBoatDetails(); }));
  boat->Add(boatActions, 0, wxEXPAND);
  auto* weather = card(_("3. Weather"));
  auto* sourceRow = new wxBoxSizer(wxHORIZONTAL);
  auto* label = new wxStaticText(content_, wxID_ANY, _("Wind source"));
  sourceRow->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 7);
  windPolicy_ = new wxChoice(content_, wxID_ANY);
  windPolicy_->Append(_("Forecast only"));
  windPolicy_->Append(_("Climatology only"));
  windPolicy_->Append(_("Forecast with Climatology fallback"));
  windPolicy_->SetName(_("Wind source policy"));
  windPolicy_->SetToolTip(
      _("Climatology describes typical seasonal conditions. Fallback permits "
        "its use where forecast wind is unavailable."));
  sourceRow->Add(windPolicy_, 1, wxEXPAND | wxALL, 5);
  mutableControls_.push_back(windPolicy_);
  windPolicy_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    auto* r = routing_.FirstCurrentRouteMap();
    if (!r) return;
    auto c = r->GetConfiguration();
    const int policy = windPolicy_->GetSelection();
    c.UseGrib = policy != 1;
    if (policy == 0) {
      if (c.ClimatologyType > RouteMapConfiguration::CURRENTS_ONLY)
        c.ClimatologyType = c.Currents ? RouteMapConfiguration::CURRENTS_ONLY
                                       : RouteMapConfiguration::DISABLED;
    } else if (c.ClimatologyType <= RouteMapConfiguration::CURRENTS_ONLY)
      c.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
    routing_.ApplyPassageConfiguration(c);
    RefreshPassage();
  });
  weather->Add(sourceRow, 0, wxEXPAND);
  weather_ = Text(content_, weather, "");
  weather_->SetName(_("Weather status"));
  auto* weatherActions = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(Button(content_, weatherActions, _("Load GRIB"),
                                    [this] { LoadWeather(); }));
  mutableControls_.push_back(Button(content_, weatherActions,
                                    _("Download / generate GRIB"),
                                    [this] { GenerateWeather(); }));
  weather->Add(weatherActions, 0, wxEXPAND);
  auto* settings = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(Button(content_, settings, _("Passage settings"),
                                    [this] { OpenSettings(0); }));
  mutableControls_.push_back(Button(content_, settings, _("Routing controls"),
                                    [this] { OpenSettings(1); }));
  Button(content_, settings, _("Planning help"), [this] { ShowHelp(); });
  page->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  limits_ = Text(content_, page, "");
  content_->SetSizer(page);
  outer->Add(content_, 1, wxEXPAND);
  auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
  status_ = Text(this, statusRow, "");
  statusRow->GetItem(status_)->SetProportion(1);
  status_->SetName(_("Calculation readiness"));
  checksButton_ = new wxButton(this, wxID_ANY, _("Review checks"));
  checksButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowChecks(); });
  statusRow->Add(checksButton_, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
  outer->Add(statusRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  auto* compute = new wxBoxSizer(wxHORIZONTAL);
  calculate_ = Button(this, compute, _("Calculate route"), [this] {
    auto* r = routing_.FirstCurrentRouteMap();
    if (!r) return;
    wxCommandEvent e;
    if (!mutableControls_.front()->IsEnabled())
      routing_.OnStop(e);
    else {
      RefreshPassage();
      if (!readiness_.CanCalculate()) {
        ShowChecks();
        return;
      }
      if (r->GetConfiguration().IsMultiLegGenerated) {
        for (auto* leg : routing_.GetMultiLegGroupRoutes(
                 r->GetConfiguration().MultiLegGroupId))
          leg->Reset();
        routing_.OnComputeMultiLegSequence(e);
      } else {
        r->Reset();
        routing_.OnCompute(e);
      }
    }
    RefreshPassage();
  });
  calculate_->SetFont(calculate_->GetFont().Bold());
  calculate_->SetMinSize(wxSize(-1, 42));
  Button(this, compute, _("Detailed report"), [this] {
    wxCommandEvent e;
    routing_.OnReport(e);
  });
  outer->Add(compute, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  SetSizer(outer);
  Bind(wxEVT_HELP, [this](wxHelpEvent&) { ShowHelp(); });
  Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
    if (IsShownOnScreen()) RefreshPassage();
  });
  refresh_.Start(1000);
  EnsurePassage();
  RefreshPassage();
}
PassagePanel::~PassagePanel() { PrepareForShutdown(); }
void PassagePanel::PrepareForShutdown() {
  refresh_.Stop();
  if (generator_) generator_->PrepareForParentShutdown();
}
void PassagePanel::EnsurePassage() {
  if (routing_.FirstCurrentRouteMap()) return;
  if (routing_.m_WeatherRoutes.empty()) {
    auto c = routing_.DefaultConfiguration();
    c.Start.clear();
    c.End.clear();
    c.StartLat = c.StartLon = c.EndLat = c.EndLon = NAN;
    routing_.AddConfiguration(c);
  }
  SelectPassage(0);
}
void PassagePanel::SelectPassage(int index) {
  if (index < 0 || index >= int(routing_.m_WeatherRoutes.size())) return;
  auto it = routing_.m_WeatherRoutes.begin();
  std::advance(it, index);
  for (long i = 0; i < routing_.m_panel->m_lWeatherRoutes->GetItemCount();
       ++i) {
    const auto* item = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(routing_.m_panel->m_lWeatherRoutes->GetItemData(i)));
    routing_.m_panel->m_lWeatherRoutes->SetItemState(
        i, item == *it ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
  }
  routing_.OnWeatherRouteSelected();
  RefreshPassage();
}
RouteMapConfiguration PassagePanel::PlanningConfiguration(
    const RouteMapConfiguration& selected) const {
  auto c = selected;
  if (c.IsMultiLegGenerated) {
    const auto legs = routing_.GetMultiLegGroupRoutes(c.MultiLegGroupId);
    if (!legs.empty()) {
      const auto first = legs.front()->GetConfiguration(),
                 last = legs.back()->GetConfiguration();
      c.Start = first.Start;
      c.StartGUID = first.StartGUID;
      c.StartLat = first.StartLat;
      c.StartLon = first.StartLon;
      c.StartTime = first.StartTime;
      c.UseCurrentTime = first.UseCurrentTime;
      c.TimeMode = first.TimeMode;
      c.PlannedArrivalTime = first.PlannedArrivalTime;
      c.ArrivalSearchHorizonMinutes = first.ArrivalSearchHorizonMinutes;
      c.End = last.End;
      c.EndGUID = last.EndGUID;
      c.EndLat = last.EndLat;
      c.EndLon = last.EndLon;
    }
  }
  for (bool start : {true, false}) {
    const auto& guid = start ? c.StartGUID : c.EndGUID;
    PlugIn_Waypoint mark;
    if (!guid.empty() && GetSingleWaypoint(guid, &mark)) {
      if (start) {
        c.StartLat = mark.m_lat;
        c.StartLon = mark.m_lon;
      } else {
        c.EndLat = mark.m_lat;
        c.EndLon = mark.m_lon;
      }
    }
  }
  return c;
}
void PassagePanel::RefreshPassage() {
  wxArrayString names;
  int selected = -1, index = 0;
  auto* current = routing_.FirstCurrentRouteMap();
  for (auto* item : routing_.m_WeatherRoutes) {
    const auto c = item->routemapoverlay->GetConfiguration();
    names.Add(c.IsMultiLegGenerated
                  ? c.MultiLegParentRouteName +
                        wxString::Format(_(" • leg %d/%d"), c.MultiLegLegIndex,
                                         c.MultiLegLegCount)
              : c.Start.empty() ? _("New passage")
                                : c.Start + " to " + c.End);
    if (item->routemapoverlay == current) selected = index;
    ++index;
  }
  // wx 3.3 exports its string vector from the toolkit DLL. Comparing that
  // vector with newer MSVC STL helpers can import symbols absent from the
  // matched SDK. Compare via the stable wx array API instead.
  const auto displayed = passages_->GetStrings();
  bool sameNames = displayed.GetCount() == names.GetCount();
  for (size_t i = 0; sameNames && i < names.GetCount(); ++i)
    sameNames = displayed.Item(i) == names.Item(i);
  if (!sameNames) passages_->Set(names);
  if (selected >= 0) passages_->SetSelection(selected);
  if (!current) return;
  const auto c = PlanningConfiguration(current->GetConfiguration());
  const bool running = !routing_.m_RunningRouteMaps.empty() ||
                       !routing_.m_WaitingRouteMaps.empty() ||
                       routing_.m_RoutePreparationDepth ||
                       routing_.m_DeferredRoutingStartPending;
  weather_routing::PassageInputs input;
  input.positionsValid = !c.Start.empty() && !c.End.empty() &&
                         std::isfinite(c.StartLat) &&
                         std::isfinite(c.StartLon) && std::isfinite(c.EndLat) &&
                         std::isfinite(c.EndLon) &&
                         std::abs(c.StartLat) <= 90 && std::abs(c.EndLat) <= 90;
  const double lonDifference = std::remainder(c.StartLon - c.EndLon, 360.0);
  input.distinctPositions =
      std::abs(c.StartLat - c.EndLat) > 1e-8 || std::abs(lonDifference) > 1e-8;
  const bool arrival =
      c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME;
  input.timeValid = arrival ? c.PlannedArrivalTime.IsValid() &&
                                  c.ArrivalSearchHorizonMinutes > 0
                            : c.UseCurrentTime || c.StartTime.IsValid();
  input.coverageChecked = input.positionsValid && input.timeValid;
  wxDateTime departure = c.UseCurrentTime ? wxDateTime::Now() : c.StartTime;
  if (arrival && c.PlannedArrivalTime.IsValid())
    departure = c.PlannedArrivalTime -
                wxTimeSpan::Minutes(c.ArrivalSearchHorizonMinutes);
  wxString when =
      arrival ? _("Target arrival: ") +
                    (c.PlannedArrivalTime.IsValid()
                         ? c.PlannedArrivalTime.Format("%d %b %Y, %H:%M",
                                                       wxDateTime::UTC)
                         : _("not set"))
              : _("Departure: ") +
                    (input.timeValid
                         ? departure.Format("%d %b %Y, %H:%M", wxDateTime::UTC)
                         : _("not set"));
  wxString route = input.positionsValid
                       ? c.Start + "  →  " + c.End + "\n" + when + " UTC"
                       : _("Choose the start, destination and departure or "
                           "target arrival time.");
  if (c.IsMultiLegGenerated) {
    route += wxString::Format(_("\nLeg %d of %d • "), c.MultiLegLegIndex,
                              c.MultiLegLegCount) +
             c.MultiLegParentRouteName;
    route += _("\nCalculate runs this passage's legs in sequence.");
  }
  if (c.IsMultiLegGenerated) {
    std::vector<RouteWaypointInfo> waypoints;
    wxString error;
    const auto legs = routing_.GetMultiLegGroupRoutes(c.MultiLegGroupId);
    input.routeSequenceValid =
        ExtractOpenCPNRouteWaypoints(c.MultiLegParentRouteGUID, waypoints,
                                     error) &&
        waypoints.size() == legs.size() + 1;
    if (input.routeSequenceValid)
      for (size_t i = 0; i < legs.size(); ++i) {
        const auto leg = legs[i]->GetConfiguration();
        if (leg.StartGUID != waypoints[i].guid ||
            leg.EndGUID != waypoints[i + 1].guid)
          input.routeSequenceValid = false;
      }
  }
  input.boatError = boatModel_.OpenXML(c.boatFileName, true, false);
  input.boatValid = input.boatError.empty() && !boatModel_.Polars.empty();
  input.boatWarnings = boatModel_.loadWarnings;
  input.exampleBoat =
      wxFileName(c.boatFileName).GetName() == "Example-40ft-monohull";
  for (const auto& polar : boatModel_.Polars)
    input.exampleBoat = input.exampleBoat ||
                        polar.FileName.Contains("40ft-monohull-example.pol");
  auto& provider = routing_.PassageForecast();
  const bool climate = c.ClimatologyType > RouteMapConfiguration::CURRENTS_ONLY;
  const int policy = !c.UseGrib ? (climate ? 1 : wxNOT_FOUND) : climate ? 2 : 0;
  windPolicy_->SetSelection(policy);
  input.policy =
      policy == wxNOT_FOUND ? weather_routing::PassageWindPolicy::None
      : policy == 1         ? weather_routing::PassageWindPolicy::Climatology
      : policy == 2
          ? weather_routing::PassageWindPolicy::ForecastWithClimatology
          : weather_routing::PassageWindPolicy::Forecast;
  input.forecastLoaded = provider.Loaded();
  wxDateTime sample = departure;
  if (arrival && input.forecastLoaded && sample.IsValid() &&
      c.PlannedArrivalTime.IsValid() &&
      provider.FirstWindTime() <= c.PlannedArrivalTime.GetTicks() &&
      provider.FirstWindTime() > sample.GetTicks())
    sample = wxDateTime(provider.FirstWindTime());
  if (input.positionsValid && input.timeValid && sample.IsValid()) {
    const auto time = sample.GetTicks();
    input.departureWind = provider.HasWind(time, c.StartLat, c.StartLon);
    input.destinationWind = provider.HasWind(time, c.EndLat, c.EndLon);
    input.wavesAvailable =
        c.UseGrib && provider.HasData(Idx_HTSIGW, time, c.StartLat, c.StartLon);
    input.currentsAvailable =
        c.UseGrib &&
        provider.HasData(Idx_SEACURRENT_VX, time, c.StartLat, c.StartLon) &&
        provider.HasData(Idx_SEACURRENT_VY, time, c.StartLat, c.StartLon);
    if (!running && c.ClimatologyType != RouteMapConfiguration::DISABLED) {
      std::lock_guard<std::recursive_mutex> lock(
          ClimatologyThreadGuard::InvocationMutex());
      double direction = 0, speed = 0;
      if (c.ClimatologyType == RouteMapConfiguration::AVERAGE)
        climateWindReady_ =
            RouteMap::ClimatologyData &&
            RouteMap::ClimatologyData(0, sample, c.StartLat, c.StartLon,
                                      direction, speed);
      else {
        int count = 8;
        double frequencies[8]{}, speeds[8]{}, gale = 0, calm = 0;
        climateWindReady_ = RouteMap::ClimatologyWindAtlasData &&
                            RouteMap::ClimatologyWindAtlasData(
                                sample, c.StartLat, c.StartLon, count,
                                frequencies, speeds, gale, calm);
      }
      if (c.Currents && RouteMap::ClimatologyData)
        input.currentsAvailable =
            input.currentsAvailable ||
            RouteMap::ClimatologyData(1, sample, c.StartLat, c.StartLon,
                                      direction, speed);
    }
    input.climatologyWind = climate && climateWindReady_;
    const double distance =
        DistGreatCircle_Plugin(c.StartLat, c.StartLon, c.EndLat, c.EndLon);
    input.estimatedFinish =
        arrival ? c.PlannedArrivalTime
                : departure + wxTimeSpan::Hours(
                                  std::max(1, int(std::ceil(distance / 4.0))));
  }
  if (provider.Loaded())
    input.forecastEnd = wxDateTime(provider.LastWindTime());
  input.useCurrents = c.Currents;
  input.requireWaves = c.RequireWaveCoverage;
  input.windLimit = c.MaxTrueWindKnots;
  input.apparentWindLimit = c.MaxApparentWindKnots;
  input.waveLimit = c.MaxSwellMeters;
  input.avoidLand = c.DetectLand;
  readiness_ = weather_routing::EvaluatePassage(input);
  const auto sectionStatus = [&](int section) {
    return weather_routing::PassageSectionStatus(readiness_.sections[section]);
  };
  route_->SetLabel(sectionStatus(0) + " • " + route);
  wxString boat = !boatModel_.Name.empty()
                      ? boatModel_.Name
                      : DisplayName(wxFileName(c.boatFileName).GetName());
  if (!input.boatValid)
    boat = input.boatError;
  else {
    boat += wxString::Format(_("\n%d polar configuration(s)"),
                             int(boatModel_.Polars.size()));

    for (const auto& polar : boatModel_.Polars) {
      boat += "\n" + (polar.Name.empty()
                          ? DisplayName(wxFileName(polar.FileName).GetName())
                          : polar.Name);
      if (!polar.SailConfiguration.empty())
        boat += " • " + polar.SailConfiguration;
      boat += " • " + (polar.Source.empty()
                           ? (input.exampleBoat ? _("illustrative estimate")
                                                : _("source unknown"))
                           : polar.Source);
      if (boat.length() > 550) {
        boat += _("\nMore polars in Boat details.");
        break;
      }
    }
  }
  boat_->SetLabel(sectionStatus(1) + " • " + boat);
  wxString weather =
      c.UseGrib ? provider.Summary() + "\n" + provider.Details()
      : climate ? _("Typical seasonal conditions, not a weather forecast")
                : _("No wind source selected");
  if (climate) weather += "\n" + weather_routing::BundledClimatology::Source();
  if (!readiness_.sections[2].items.empty() &&
      !weather.Contains(readiness_.sections[2].items.front().title))
    weather += "\n" + readiness_.sections[2].items.front().title;
  weather_->SetLabel(sectionStatus(2) + " • " + weather);
  limits_->SetLabel(wxString::Format(
      _("Passage limits: wind %.0f kn • waves %.1f m • %s • %s"),
      c.MaxTrueWindKnots, c.MaxSwellMeters,
      c.Currents ? _("currents on") : _("currents off"),
      c.DetectLand ? _("land avoidance on") : _("land avoidance off")));
  const auto report = readiness_.Report();
  wxString state = readiness_.CanCalculate() ? _("Ready to calculate")
                                             : _("Setup needs attention");
  for (const auto& issue : report.items)
    if (issue.severity == weather_routing::DiagnosticSeverity::Error) {
      state = issue.title;
      break;
    }
  if (readiness_.Warnings())
    state += wxString::Format(_(" • %u to review"), readiness_.Warnings());
  if (current->Finished() && readiness_.CanCalculate())
    state = current->GetDiagnostics().Summary();
  status_->SetLabel(
      running ? _("Calculating passage… Setup is preserved if you cancel.")
              : state);
  checksButton_->SetLabel(readiness_.CanCalculate() ? _("Review checks")
                                                    : _("Resolve setup"));
  calculate_->SetLabel(running ? _("Cancel calculation")
                               : _("Calculate route"));
  calculate_->Enable(running || readiness_.CanCalculate());
  for (auto* control : mutableControls_) control->Enable(!running);
  RefreshResults(current, running);
  if (!running && current->Finished() && readiness_.CanCalculate())
    status_->SetLabel(resultStatus_);
  for (auto* t : {route_, boat_, weather_, limits_, results_}) {
    t->Wrap(std::max(220, content_->GetClientSize().x - 64));
    // A wrapped label's previous best width must not become the scroller's
    // minimum virtual width after the user narrows the window.
    t->SetMinSize(wxSize(1, -1));
  }
  status_->Wrap(std::max(180, GetClientSize().x - 230));
  content_->Layout();
  content_->FitInside();
  Layout();
  wxString layoutTrace;
  static bool traced = false;
  if (!traced && IsShownOnScreen() && wxGetEnv("WR_UI_LAYOUT_DIAGNOSTICS", &layoutTrace)) {
    traced = true;
    const auto frame = GetParent()->GetParent()->GetClientSize();
    const auto notebook = GetParent()->GetClientSize();
    const auto size = GetClientSize();
    wxLogMessage("V2_LAYOUT frame=%dx%d notebook=%dx%d page=%dx%d content=%dx%d calculate_y=%d height=%d", frame.x, frame.y, notebook.x, notebook.y, size.x, size.y, content_->GetSize().x, content_->GetSize().y, calculate_->GetPosition().y, calculate_->GetSize().y);
  }
}

void PassagePanel::NewPassage(bool example) {
  auto* selected = routing_.FirstCurrentRouteMap();
  auto c =
      selected ? selected->GetConfiguration() : routing_.DefaultConfiguration();
  // Start a new passage, retaining the vessel and planning preferences, never
  // the identity or computed scheduling state of a linked leg or candidate.
  c.RouteGUID.clear();
  c.Start.clear();
  c.End.clear();
  c.StartGUID.clear();
  c.EndGUID.clear();
  c.StartType = RouteMapConfiguration::START_FROM_POSITION;
  c.EndType = RouteMapConfiguration::END_AT_POSITION;
  c.StartLat = c.StartLon = c.EndLat = c.EndLon = NAN;
  c.UseCurrentTime = false;
  c.StartTime = wxDateTime::Now();
  c.TimeMode = RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
  c.PlannedArrivalTime = c.StartTime + wxTimeSpan::Hours(24);
  c.ArrivalPlanningEvaluatedRoutes = c.ArrivalPlanningFeasibleRoutes = 0;
  c.ArrivalPlanningScheduleMarginSeconds = 0;
  c.DepartureTimeOptimizationEnabled = false;
  c.PromoteDepartureTimeOptimizationCandidate();
  c.IsMultiLegGenerated = false;
  c.MultiLegGroupId.clear();
  c.MultiLegParentRouteGUID.clear();
  c.MultiLegParentRouteName.clear();
  c.MultiLegLegIndex = c.MultiLegLegCount = 0;
  if (example) {
    c.Start = _("Example Atlantic west");
    c.End = _("Example Atlantic east");
    c.StartLat = 30;
    c.StartLon = -40;
    c.EndLat = 30.3;
    c.EndLon = -39.5;
    routing_.AddPosition(c.StartLat, c.StartLon, c.Start);
    routing_.AddPosition(c.EndLat, c.EndLon, c.End);
    c.boatFileName =
        weather_routing_pi::StandardPath() + "boats/Example-40ft-monohull.xml";
    c.UseGrib = false;
    c.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
    c.Currents = false;
    c.UseMotor = false;
    c.DetectLand = true;
    c.RequireWaveCoverage = false;
    c.MaxTrueWindKnots = weather_routing::PassageDefaults::windKnots;
    c.MaxApparentWindKnots =
        weather_routing::PassageDefaults::apparentWindKnots;
    c.MaxSwellMeters = weather_routing::PassageDefaults::waveMeters;
  }
  if (!routing_.AddConfiguration(c)) return;
  SelectPassage(int(routing_.m_WeatherRoutes.size()) - 1);
  if (!example)
    EditRoute();
  else
    ShowHelp();
}
void PassagePanel::RestoreDefaults() {
  auto* route = routing_.FirstCurrentRouteMap();
  if (!route) return;
  wxDialog dialog(this, wxID_ANY, _("Restore passage preferences"));
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       _("Restore these starting preferences for the selected passage:\n\n"
         "Maximum true wind: 30 kn\nMaximum apparent wind: 40 kn\nMaximum "
         "waves: 3 m\n"
         "Land avoidance: on\nCurrents and motoring: off\nWind and boat "
         "performance adjustments: 100%\n\n"
         "The route, passage time, boat, weather source, depth and shore "
         "margins, "
         "required wave coverage and routing controls are retained."));
  s->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
  dialog.SetSizerAndFit(s);
  dialog.CentreOnParent();
  if (dialog.ShowModal() != wxID_OK) return;
  auto c = route->GetConfiguration();
  c.MaxTrueWindKnots = weather_routing::PassageDefaults::windKnots;
  c.MaxApparentWindKnots = weather_routing::PassageDefaults::apparentWindKnots;
  c.MaxSwellMeters = weather_routing::PassageDefaults::waveMeters;
  c.DetectLand = true;
  c.Currents = false;
  c.UseMotor = false;
  c.WindStrength = c.UpwindEfficiency = c.DownwindEfficiency =
      c.NightCumulativeEfficiency = 1;
  routing_.ApplyPassageConfiguration(c);
  RefreshPassage();
}
void PassagePanel::ChooseRoute() {
  std::vector<wxString> guids;
  wxArrayString labels;
  for (const auto& guid : GetRouteGUIDArray()) {
    auto route = GetRoute_Plugin(guid);
    if (!route || !route->pWaypointList || route->pWaypointList->GetCount() < 2)
      continue;
    guids.push_back(guid);
    labels.Add((route->m_NameString.empty() ? _("Unnamed route")
                                            : route->m_NameString) +
               wxString::Format(_(" • %zu waypoints"),
                                route->pWaypointList->GetCount()));
  }
  if (guids.empty()) {
    wxMessageBox(
        _("Create a route with at least two waypoints on the OpenCPN chart, "
          "then choose it here. You can also right-click that route to start "
          "weather routing."),
        _("Use OpenCPN route"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  wxSingleChoiceDialog chooser(
      this,
      _("Choose a route. Its waypoints become a continuous "
        "passage whose legs are calculated in sequence."),
      _("Use OpenCPN route"), labels);
  if (chooser.ShowModal() != wxID_OK) return;
  const int first = int(routing_.m_WeatherRoutes.size());
  if (routing_.CreateMultiLegConfigurationsFromRoute(
          guids.at(chooser.GetSelection()), false))
    SelectPassage(first);
}
void PassagePanel::ShowWaypoints() {
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  const auto c = r->GetConfiguration();
  const auto guid =
      c.IsMultiLegGenerated ? c.MultiLegParentRouteGUID : c.RouteGUID;
  std::vector<RouteWaypointInfo> points;
  wxString error;
  if (!guid.empty())
    ExtractOpenCPNRouteWaypoints(guid, points, error);
  else if (!c.Start.empty() && !c.End.empty()) {
    points.push_back({c.StartGUID, c.Start, c.StartLat, c.StartLon, 0});
    points.push_back({c.EndGUID, c.End, c.EndLat, c.EndLon, 1});
  }
  wxDialog dialog(this, wxID_ANY, _("Passage waypoints"), wxDefaultPosition,
                  wxSize(640, 460), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       guid.empty()
           ? _("Named positions for this passage.")
           : _("Waypoints are owned by the OpenCPN route. To edit them, show "
               "the route on "
               "the chart and open its properties. If you add, remove or "
               "reorder waypoints, "
               "use OpenCPN route again to create a new passage sequence."));
  if (!error.empty()) Text(&dialog, s, error);
  auto* list = new wxListBox(&dialog, wxID_ANY);
  list->SetName(_("Passage waypoints in order"));
  for (size_t i = 0; i < points.size(); ++i)
    list->Append(
        wxString::Format("%zu. ", i + 1) + points[i].name +
        wxString::Format("   %.5f, %.5f", points[i].lat, points[i].lon));
  s->Add(list, 1, wxEXPAND | wxALL, 10);
  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  Button(&dialog, buttons, _("Show on chart"), [&, this] {
    wxCommandEvent e;
    routing_.OnGoTo(e);
    dialog.EndModal(wxID_OK);
    routing_.Hide();
  });
  Button(&dialog, buttons, _("Close"), [&] { dialog.EndModal(wxID_CANCEL); });
  s->Add(buttons, 0, wxEXPAND | wxALL, 10);
  dialog.SetSizer(s);
  dialog.CentreOnParent();
  dialog.ShowModal();
}
void PassagePanel::OpenSettings(size_t section) {
  EnsurePassage();
  routing_.m_ConfigurationDialog.ShowSettingsSection(section);
  auto* route = routing_.FirstCurrentRouteMap();
  if (route && route->GetConfiguration().IsMultiLegGenerated) {
    wxCommandEvent event;
    routing_.OnEditMultiLegGroupSettings(event);
  } else
    routing_.OnEditConfiguration();
}
void PassagePanel::ShowChecks() {
  RefreshPassage();
  wxDialog dialog(this, wxID_ANY, _("Passage checks"), wxDefaultPosition,
                  wxSize(680, 540), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  auto* scroll = new wxScrolledWindow(&dialog);
  scroll->SetScrollRate(0, 14);
  auto* body = new wxBoxSizer(wxVERTICAL);
  const wxString sections[] = {_("Route"), _("Boat and polars"), _("Weather"),
                               _("Passage limits")};
  for (size_t i = 0; i < readiness_.sections.size(); ++i) {
    const auto& section = readiness_.sections[i];
    Text(scroll, body,
         sections[i] + ": " + weather_routing::PassageSectionStatus(section),
         true);
    for (const auto& issue : section.items) {
      auto* text =
          Text(scroll, body,
               issue.title + (issue.detail.empty() ? wxString() : "\n" + issue.detail));
      text->Wrap(600);
      if (issue.action.empty()) continue;
      auto* action = new wxButton(scroll, wxID_ANY, issue.action);
      body->Add(action, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
      action->Enable(mutableControls_.front()->IsEnabled());
      action->Bind(wxEVT_BUTTON, [&, code = issue.code](wxCommandEvent&) {
        dialog.EndModal(wxID_OK);
        // Defer until the modal stack unwinds, and resolve current selection at
        // execution rather than retaining pointers to route models.
        CallAfter([this, code] {
          if (code == "passage.weather.waves" ||
              code == "passage.weather.destination")
            GenerateWeather();
          else if (code == "passage.route.sequence")
            ChooseRoute();
          else
            switch (weather_routing::RecoveryAction(code)) {
              case weather_routing::PassageAction::Route:
                EditRoute();
                break;
              case weather_routing::PassageAction::Boat:
                ShowLibrary();
                break;
              case weather_routing::PassageAction::Weather:
                windPolicy_->SetFocus();
                break;
              case weather_routing::PassageAction::Limits:
                OpenSettings(0);
                break;
            }
        });
      });
    }
  }
  if (auto* route = routing_.FirstCurrentRouteMap();
      route && route->Finished()) {
    const auto report = route->GetDiagnostics();
    Text(scroll, body, _("Last calculation: ") + report.Summary(), true)
        ->Wrap(600);
    for (const auto& item : report.items)
      if (item.severity != weather_routing::DiagnosticSeverity::Notice)
        Text(scroll, body,
             item.title + (item.detail.empty() ? wxString() : "\n" + item.detail))
            ->Wrap(600);
    auto* details = new wxButton(scroll, wxID_ANY, _("Open detailed report"));
    body->Add(details, 0, wxALL, 8);
    details->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
      dialog.EndModal(wxID_OK);
      CallAfter([this] {
        wxCommandEvent e;
        routing_.OnReport(e);
      });
    });
  }
  Text(scroll, body,
       _("These checks establish whether calculation can start. "
         "Weather coverage, detours and constraints are also checked during "
         "routing and final validation."))
      ->Wrap(600);
  scroll->SetSizer(body);
  s->Add(scroll, 1, wxEXPAND);
  s->Add(dialog.CreateButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 10);
  dialog.SetSizer(s);
  dialog.CentreOnParent();
  dialog.ShowModal();
}
void PassagePanel::ShowHelp() {
  wxDialog dialog(this, wxID_ANY, _("Planning a passage"));
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(
      &dialog, s,
      _("Route\nChoose your own positions or an existing OpenCPN route. "
        "Departure and target arrival times are UTC. OpenCPN routes retain "
        "their waypoint identities; "
        "their legs run in sequence.\n\nBoat and polars\nThe example 40 ft "
        "monohull is illustrative. "
        "Choose your vessel's data, or duplicate a library polar and edit your "
        "own copy. "
        "Unknown source information is shown honestly and does not prevent "
        "using a polar.\n\n"
        "Weather\nForecast only uses the selected GRIB. Climatology uses "
        "typical seasonal conditions. "
        "Forecast with Climatology fallback allows typical conditions where "
        "wind forecasts are missing. "
        "A loaded GRIB is retained when you change source policy. Missing "
        "waves or currents remain "
        "unknown; Review checks explains their effect.\n\nCalculate\nReadiness "
        "means the inputs "
        "permit a calculation. Review operating limits for your vessel and "
        "passage. "
        "The result shows actual arrival and source use, with further checks "
        "in the report.\n\n"
        "New passage keeps the boat and preferences. Passage actions contains "
        "a separate, optional "
        "example and a scoped restore action. All settings remain available."));
  s->Add(dialog.CreateButtonSizer(wxOK), 0, wxEXPAND | wxALL, 12);
  dialog.SetSizerAndFit(s);
  dialog.CentreOnParent();
  dialog.ShowModal();
}
void PassagePanel::RefreshResults(RouteMapOverlay* current, bool busy) {
  if (busy || !current->Finished()) {
    resultsBox_->Show(false);
    resultRoute_ = nullptr;
    resultKey_.clear();
    resultStatus_.clear();
    return;
  }
  const auto config = current->GetConfiguration();
  auto routes = config.IsMultiLegGenerated
                    ? routing_.GetMultiLegGroupRoutes(config.MultiLegGroupId)
                    : std::vector<RouteMapOverlay*>{current};
  wxString key;
  for (auto* r : routes)
    key +=
        r->GetDiagnostics().Summary() + r->ModernNativeCoverageNotes() +
        (r->EndTime().IsValid() ? r->EndTime().FormatISOCombined() : wxString("pending"));
  if (current == resultRoute_ && key == resultKey_) {
    resultsBox_->Show(true);
    return;
  }
  resultRoute_ = current;
  resultKey_ = key;
  unsigned complete = 0, warnings = 0;
  double distance = 0, wind = NAN, waves = NAN;
  bool missingWaves = false, usedClimate = false, missingWind = false;
  wxDateTime start, end;
  for (auto* r : routes) {
    const auto report = r->GetDiagnostics();
    warnings += report.Count(weather_routing::DiagnosticSeverity::Warning);
    if (!r->Finished() || !r->ReachedDestination()) continue;
    ++complete;
    missingWaves = missingWaves || r->HasUnknownWaveHeights() ||
                   !r->GetConfiguration().UseGrib;
    distance += r->RouteInfo(RouteMapOverlay::DISTANCE);
    const double w = r->RouteInfo(RouteMapOverlay::MAXWIND),
                 h = r->RouteInfo(RouteMapOverlay::MAXSWELL);
    if (std::isfinite(w)) wind = std::isfinite(wind) ? std::max(wind, w) : w;
    if (std::isfinite(h) && h >= 0)
      waves = std::isfinite(waves) ? std::max(waves, h) : h;
    if (!start.IsValid() || r->StartTime() < start) start = r->StartTime();
    if (!end.IsValid() || r->EndTime() > end) end = r->EndTime();
    for (const auto& item : report.items) {
      usedClimate = usedClimate || item.code == "usage.climatology_wind";
      missingWind = missingWind || item.code == "usage.missing_wind";
      missingWaves = missingWaves || item.code == "usage.missing_waves";
    }
  }
  wxString summary;
  if (!complete)
    summary = current->GetDiagnostics().Summary() +
              _("\nNo completed passage. Review the detailed report before "
                "recalculating.");
  else {
    if (routes.size() > 1)
      summary += wxString::Format(_("%u of %zu legs complete\n"), complete,
                                  routes.size());
    summary += _("Arrival: ") + end.Format("%d %b %Y, %H:%M", wxDateTime::UTC) +
               " UTC";
    const auto duration = end - start;
    summary += wxString::Format(
        _(" • Duration: %ld h %ld min\nDistance: %.1f nm"),
        static_cast<long>(duration.GetHours()),
        static_cast<long>(duration.GetMinutes() % 60), distance);
    const double hours = duration.GetSeconds().ToDouble() / 3600;
    if (hours > 0)
      summary += wxString::Format(_(" • Average over ground: %.1f kn"),
                                  distance / hours);
    summary +=
        _("\nMaximum wind: ") + (std::isfinite(wind) && !missingWind
                                     ? wxString::Format(_("%.1f kn"), wind)
                                     : _("not fully available"));
    summary +=
        _(" • Maximum waves: ") + (std::isfinite(waves) && !missingWaves
                                       ? wxString::Format(_("%.1f m"), waves)
                                       : _("not fully available"));
    summary +=
        _("\nWind used: ") +
        (usedClimate ? _("Climatology was used; intervals are in the report")
         : current->UsesModernNativeResult() && config.UseGrib && !missingWind
             ? _("forecast")
         : !config.UseGrib ? _("Climatology")
                           : _("see detailed report"));
    summary += wxString::Format(_(" • %u warnings"), warnings);
  }
  resultStatus_ =
      routes.size() > 1
          ? complete == routes.size()
                ? wxString::Format(
                      _("Passage complete • %u warnings across %zu legs"),
                      warnings, routes.size())
                : wxString::Format(
                      _("Passage incomplete • %u of %zu legs complete"),
                      complete, routes.size())
          : current->GetDiagnostics().Summary();
  results_->SetLabel(summary);
  resultsBox_->Show(true);
}

void PassagePanel::EditRoute() {
  EnsurePassage();
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  auto c = r->GetConfiguration();
  if (c.IsMultiLegGenerated && c.MultiLegLegIndex > 1) {
    int index = 0;
    for (auto* item : routing_.m_WeatherRoutes) {
      const auto first = item->routemapoverlay->GetConfiguration();
      if (first.MultiLegGroupId == c.MultiLegGroupId &&
          first.MultiLegLegIndex == 1) {
        SelectPassage(index);
        EditRoute();
        return;
      }
      ++index;
    }
  }
  std::vector<RouteMapPosition> choices(RouteMap::Positions.begin(),
                                        RouteMap::Positions.end());
  for (const auto& guid : GetWaypointGUIDArray()) {
    PlugIn_Waypoint mark;
    if (!GetSingleWaypoint(guid, &mark)) continue;
    bool duplicate = false;
    for (const auto& p : choices) duplicate = duplicate || p.GUID == guid;
    if (!duplicate)
      choices.emplace_back(mark.m_MarkName, mark.m_lat, mark.m_lon, guid);
  }
  wxDialog dialog(this, wxID_ANY, _("Define passage"), wxDefaultPosition,
                  wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  const bool linked = !c.RouteGUID.empty() || c.IsMultiLegGenerated;
  auto* top = new wxBoxSizer(wxVERTICAL);
  Text(
      &dialog, top,
      _("Select saved positions or enter coordinates in decimal degrees. Chart "
        "positions can also be added from the chart's right-click menu."));
  auto* grid = new wxFlexGridSizer(2, 8, 8);
  grid->AddGrowableCol(1);
  wxComboBox* name[2];
  wxTextCtrl* lat[2];
  wxTextCtrl* lon[2];
  for (int i = 0; i < 2; ++i) {
    grid->Add(
        new wxStaticText(&dialog, wxID_ANY, i ? _("Destination") : _("Start")),
        0, wxALIGN_CENTER_VERTICAL);
    name[i] = new wxComboBox(&dialog, wxID_ANY, i ? c.End : c.Start);
    for (const auto& p : choices)
      name[i]->Append(p.Name + (p.GUID.empty() ? wxString() : _(" (waypoint)")));
    grid->Add(name[i], 1, wxEXPAND);
    grid->Add(
        new wxStaticText(&dialog, wxID_ANY, _("Latitude (north +, south −)")),
        0, wxALIGN_CENTER_VERTICAL);
    lat[i] =
        new wxTextCtrl(&dialog, wxID_ANY,
                       std::isfinite(i ? c.EndLat : c.StartLat)
                           ? wxString::Format("%.6f", i ? c.EndLat : c.StartLat)
                           : wxString());
    grid->Add(lat[i], 1, wxEXPAND);
    grid->Add(
        new wxStaticText(&dialog, wxID_ANY, _("Longitude (east +, west −)")), 0,
        wxALIGN_CENTER_VERTICAL);
    lon[i] =
        new wxTextCtrl(&dialog, wxID_ANY,
                       std::isfinite(i ? c.EndLon : c.StartLon)
                           ? wxString::Format("%.6f", i ? c.EndLon : c.StartLon)
                           : wxString());
    grid->Add(lon[i], 1, wxEXPAND);
    if (linked) {
      name[i]->Enable(false);
      lat[i]->Enable(false);
      lon[i]->Enable(false);
    }
    name[i]->Bind(wxEVT_COMBOBOX, [&, i](wxCommandEvent&) {
      const int selected = name[i]->GetSelection();
      if (selected >= 0) {
        const auto& p = choices.at(selected);
        lat[i]->SetValue(wxString::Format("%.6f", p.lat));
        lon[i]->SetValue(wxString::Format("%.6f", p.lon));
      }
    });
  }
  auto* mode = new wxChoice(&dialog, wxID_ANY);
  mode->Append(_("Depart at"));
  mode->Append(_("Arrive by"));
  mode->SetSelection(
      c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME ? 1 : 0);
  grid->Add(mode, 0, wxEXPAND);
  auto* time =
      new wxTextCtrl(&dialog, wxID_ANY,
                     (mode->GetSelection() ? c.PlannedArrivalTime : c.StartTime)
                         .Format("%Y-%m-%d %H:%M", wxDateTime::UTC));
  grid->Add(time, 1, wxEXPAND);
  top->Add(grid, 1, wxEXPAND | wxALL, 15);
  Text(&dialog, top,
       _("Time is UTC, in YYYY-MM-DD HH:MM format. For intermediate waypoints, "
         "right-click an OpenCPN route and choose Create Weather Routing "
         "Legs."));
  if (linked)
    Text(&dialog, top,
         _("These waypoints follow the OpenCPN route. Edit that route on the "
           "chart to change its waypoints. A continuous passage starts at its "
           "first leg."));
  top->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
  dialog.SetSizerAndFit(top);
  dialog.SetMinSize(wxSize(620, -1));
  dialog.CentreOnParent();
  auto* ok = dialog.FindWindow(wxID_OK);
  ok->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    double a[2], b[2];
    wxDateTime date;
    bool valid = !!date.ParseFormat(time->GetValue(), "%Y-%m-%d %H:%M") &&
                 date.IsValid();
    if (linked && valid) {
      c.UseCurrentTime = false;
      c.TimeMode = mode->GetSelection()
                       ? RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                       : RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
      if (mode->GetSelection())
        c.PlannedArrivalTime = date.FromUTC();
      else
        c.StartTime = date.FromUTC();
      dialog.EndModal(wxID_OK);
      return;
    }
    for (int i = 0; i < 2; ++i)
      valid = lat[i]->GetValue().ToDouble(&a[i]) &&
              lon[i]->GetValue().ToDouble(&b[i]) && std::isfinite(a[i]) &&
              std::isfinite(b[i]) && std::abs(a[i]) <= 90 &&
              std::abs(b[i]) <= 180 && !name[i]->GetValue().empty() && valid;
    if (!valid) {
      wxMessageBox(_("Enter two named positions with valid coordinates and a "
                     "UTC date and time."),
                   _("Check passage"), wxOK | wxICON_INFORMATION, &dialog);
      return;
    }
    if (a[0] == a[1] && b[0] == b[1]) {
      wxMessageBox(_("Start and destination must be different."),
                   _("Check passage"), wxOK, &dialog);
      return;
    }
    if (name[0]->GetValue() == name[1]->GetValue()) {
      wxMessageBox(_("Give the start and destination different names."),
                   _("Check passage"), wxOK, &dialog);
      return;
    }
    wxString guids[2];
    for (int i = 0; i < 2; ++i) {
      const int selected = name[i]->GetSelection();
      if (selected >= 0) {
        const auto& point = choices.at(selected);
        if (std::abs(point.lat - a[i]) < 1e-6 &&
            std::abs(point.lon - b[i]) < 1e-6 && !point.GUID.empty()) {
          guids[i] = point.GUID;
          name[i]->SetValue(point.Name);
          continue;
        }
      }
      bool exists = false;
      for (const auto& p : RouteMap::Positions)
        if (p.Name == name[i]->GetValue()) {
          if (p.lat == a[i] && p.lon == b[i])
            exists = true;
          else
            name[i]->SetValue(
                name[i]->GetValue() +
                wxString::Format(" (%lld)", wxGetUTCTimeMillis().GetValue()));
          break;
        }
      if (!exists) routing_.AddPosition(a[i], b[i], name[i]->GetValue());
    }
    c.Start = name[0]->GetValue();
    c.End = name[1]->GetValue();
    c.StartLat = a[0];
    c.StartLon = b[0];
    c.EndLat = a[1];
    c.EndLon = b[1];
    c.StartType = guids[0].empty() ? RouteMapConfiguration::START_FROM_POSITION
                                   : RouteMapConfiguration::START_FROM_WAYPOINT;
    c.EndType = guids[1].empty() ? RouteMapConfiguration::END_AT_POSITION
                                 : RouteMapConfiguration::END_AT_WAYPOINT;
    c.StartGUID = guids[0];
    c.EndGUID = guids[1];
    c.UseCurrentTime = false;
    c.TimeMode = mode->GetSelection()
                     ? RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                     : RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
    if (mode->GetSelection())
      c.PlannedArrivalTime = date.FromUTC();
    else
      c.StartTime = date.FromUTC();
    dialog.EndModal(wxID_OK);
  });
  if (dialog.ShowModal() == wxID_OK) routing_.ApplyPassageConfiguration(c);
  RefreshPassage();
}

void PassagePanel::ApplyBoat(const wxString& path) {
  EnsurePassage();
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  auto c = r->GetConfiguration();
  c.boatFileName = path;
  boatModel_.OpenXML(path, false, false);
  routing_.ApplyPassageConfiguration(c);
  RefreshPassage();
}
void PassagePanel::ChooseBoat() {
  wxFileDialog chooser(
      this, _("Import boat or polar"), wxEmptyString, wxEmptyString,
      _("Boat and polar files|*.xml;*.pol;*.csv;*.txt|All files|*.*"),
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (chooser.ShowModal() != wxID_OK) return;
  if (wxFileName(chooser.GetPath()).GetExt().CmpNoCase("xml") == 0) {
    wxString imported;
    const wxString error = Boat::ImportXML(
        chooser.GetPath(), weather_routing_pi::StandardPath(), imported);
    if (!error.empty()) {
      wxMessageBox(error, _("Cannot import boat"), wxOK | wxICON_INFORMATION,
                   this);
      return;
    }
    ApplyBoat(imported);
    return;
  }
  Polar p;
  wxString error;
  if (!p.Open(chooser.GetPath(), error)) {
    wxMessageBox(error, _("Cannot import polar"), wxOK | wxICON_ERROR, this);
    return;
  }
  const wxString folder = weather_routing_pi::StandardPath();
  wxString target =
      folder + "polars/" + wxFileName(chooser.GetPath()).GetFullName();
  if (wxFileExists(target))
    target = folder + "polars/" +
             wxString::Format("import-%lld-", wxGetUTCTimeMillis().GetValue()) +
             wxFileName(chooser.GetPath()).GetFullName();
  if (!wxCopyFile(chooser.GetPath(), target, false)) {
    wxMessageBox(_("Unable to copy this polar into your boat library."));
    return;
  }
  Boat boat;
  p.FileName = target;
  boat.Polars.push_back(p);
  const wxString path =
      folder + "boats/" +
      wxString::Format("import-%lld.xml", wxGetUTCTimeMillis().GetValue());
  error = boat.SaveXML(path);
  if (!error.empty()) {
    wxMessageBox(error);
    return;
  }
  ApplyBoat(path);
}
void PassagePanel::ShowLibrary() {
  const wxString root = weather_routing_pi::StandardPath();
  wxDialog dialog(this, wxID_ANY, _("Boat and polar library"),
                  wxDefaultPosition, wxSize(700, 650),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       _("Select a saved boat or use a library polar as a starting point. "
         "Library polars are copied before editing. Source information may be "
         "unknown."));
  auto* kind = new wxChoice(&dialog, wxID_ANY);
  kind->Append(_("Saved boats"));
  kind->Append(_("Polar library"));
  kind->SetSelection(0);
  kind->SetName(_("Library category"));
  s->Add(kind, 0, wxEXPAND | wxALL, 8);
  auto* search = new wxSearchCtrl(&dialog, wxID_ANY);
  search->SetName(_("Search boat and polar names"));
  s->Add(search, 0, wxEXPAND | wxALL, 8);
  auto* list = new wxListBox(&dialog, wxID_ANY);
  list->SetName(_("Available boats and polars"));
  s->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
  auto* details = Text(&dialog, s, "");
  auto* actions = new wxBoxSizer(wxHORIZONTAL);
  auto* duplicate = new wxButton(&dialog, wxID_ANY, _("Duplicate boat"));
  auto* exportBoat = new wxButton(&dialog, wxID_ANY, _("Export boat…"));
  actions->Add(duplicate, 1, wxALL, 5);
  actions->Add(exportBoat, 1, wxALL, 5);
  s->Add(actions, 0, wxEXPAND | wxALL, 5);
  s->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
  if (auto* ok = dialog.FindWindow(wxID_OK)) ok->SetLabel(_("Use selection"));
  std::vector<wxString> filtered;
  const auto describe = [&] {
    const int index = list->GetSelection();
    const bool selected = index >= 0;
    duplicate->Enable(selected && kind->GetSelection() == 0);
    exportBoat->Enable(selected && kind->GetSelection() == 0);
    dialog.FindWindow(wxID_OK)->Enable(selected);
    wxString value;
    if (selected) {
      const auto path = filtered.at(index);
      if (kind->GetSelection() == 0) {
        TiXmlDocument doc;
        if (doc.LoadFile(path.utf8_str()) && doc.RootElement()) {
          value =
              wxString::FromUTF8(doc.RootElement()->Attribute("description"));
          for (auto* p = doc.RootElement()->FirstChildElement("Polar"); p;
               p = p->NextSiblingElement("Polar")) {
            const auto name = wxString::FromUTF8(p->Attribute("Name"));
            const auto source = wxString::FromUTF8(p->Attribute("Source"));
            const auto sails = wxString::FromUTF8(p->Attribute("Sails"));
            if (!value.empty()) value += "\n";
            value +=
                name.empty()
                    ? wxFileName(wxString::FromUTF8(p->Attribute("FileName")))
                          .GetName()
                    : name;
            value += " • " + (source.empty() ? _("source unknown") : source);
            if (!sails.empty()) value += " • " + sails;
            if (value.length() > 350) {
              value += _("\nMore details after selection.");
              break;
            }
          }
        }
      } else
        value =
            _("A personal copy will be created. Source: unknown unless "
              "documented. "
              "Add provenance and sail information in Boat details.");
    }
    details->SetLabel(value);
    details->Wrap(640);
    dialog.Layout();
  };
  const auto filter = [&] {
    wxArrayString paths;
    wxDir::GetAllFiles(root + (kind->GetSelection() == 0 ? "boats" : "polars"),
                       &paths, kind->GetSelection() == 0 ? "*.xml" : "*.pol",
                       wxDIR_FILES | wxDIR_DIRS);
    paths.Sort();
    filtered.clear();
    list->Clear();
    for (const auto& path : paths) {
      wxString name = DisplayName(wxFileName(path).GetName());
      if (kind->GetSelection() == 0) {
        TiXmlDocument doc;
        if (doc.LoadFile(path.utf8_str()) && doc.RootElement()) {
          const auto title =
              wxString::FromUTF8(doc.RootElement()->Attribute("name"));
          if (!title.empty()) name = title;
        }
      }
      if (name.Lower().Contains(search->GetValue().Lower())) {
        filtered.push_back(path);
        list->Append(name);
      }
    }
    if (list->GetCount()) list->SetSelection(0);
    describe();
  };
  kind->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { filter(); });
  search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { filter(); });
  list->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&) { describe(); });
  duplicate->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    const int index = list->GetSelection();
    if (index < 0) return;
    wxString imported;
    wxString error = Boat::ImportXML(filtered.at(index), root, imported);
    if (error.empty()) {
      Boat copy;
      error = copy.OpenXML(imported, false, false);
      if (error.empty()) {
        copy.Name = list->GetStringSelection() + _(" copy");
        error = copy.SaveXML(imported);
      }
    }
    if (!error.empty()) {
      wxMessageBox(error, _("Cannot duplicate boat"), wxOK, &dialog);
      return;
    }
    filter();
    for (size_t i = 0; i < filtered.size(); ++i)
      if (filtered[i] == imported) list->SetSelection(i);
    describe();
  });
  exportBoat->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    const int index = list->GetSelection();
    if (index < 0) return;
    wxFileDialog save(&dialog, _("Export boat and polars"), "", "boat.xml",
                      _("Boat XML|*.xml"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (save.ShowModal() != wxID_OK) return;
    const wxString error = Boat::ExportXML(filtered.at(index), save.GetPath());
    wxMessageBox(error.empty()
                     ? _("Boat exported with its polars in a folder beside the "
                         "XML file. Keep them together when sharing.")
                     : error,
                 error.empty() ? _("Boat exported") : _("Cannot export boat"),
                 wxOK, &dialog);
  });
  dialog.SetSizer(s);
  filter();
  dialog.CentreOnParent();
  search->SetFocus();
  if (dialog.ShowModal() != wxID_OK || list->GetSelection() < 0) return;
  const wxString selected = filtered.at(list->GetSelection());
  if (kind->GetSelection() == 0) {
    Boat candidate;
    const wxString error = candidate.OpenXML(selected, false, false);
    if (!error.empty() || candidate.Polars.empty()) {
      wxMessageBox(error.empty() ? _("This boat contains no polars.") : error,
                   _("Cannot use boat"), wxOK, this);
      return;
    }
    ApplyBoat(selected);
    return;
  }
  Polar polar;
  wxString error;
  if (!polar.Open(selected, error)) {
    wxMessageBox(error);
    return;
  }
  wxString id = wxString::Format("boat-%lld", wxGetUTCTimeMillis().GetValue());
  while (wxFileExists(root + "boats/" + id + ".xml")) id += "-1";
  const wxString directory = root + "polars/personal/" + id;
  if (!wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    wxMessageBox(_("Unable to create a personal polar directory."));
    return;
  }
  const wxString copy = directory + "/" + wxFileName(selected).GetFullName();
  if (!wxCopyFile(selected, copy, false)) {
    wxMessageBox(_("Unable to copy this polar."));
    return;
  }
  Boat boat;
  boat.Name = DisplayName(wxFileName(selected).GetName());
  polar.FileName = copy;
  polar.Name = boat.Name;
  boat.Polars.push_back(polar);
  const wxString path = root + "boats/" + id + ".xml";
  error = boat.SaveXML(path);
  if (!error.empty()) {
    wxMessageBox(error);
    return;
  }
  ApplyBoat(path);
  routing_.m_ConfigurationDialog.EditBoat();
}

void PassagePanel::EditBoatDetails() {
  if (routing_.m_BoatDialog.IsShown()) {
    routing_.m_BoatDialog.Raise();
    wxMessageBox(
        _("Save or close the polar editor before editing the boat details."),
        _("Boat details"), wxOK, this);
    return;
  }
  auto* route = routing_.FirstCurrentRouteMap();
  if (!route) return;
  const auto path = route->GetConfiguration().boatFileName;
  Boat boat;
  const auto error = boat.OpenXML(path, false, false);
  if (!error.empty() || boat.Polars.empty()) {
    wxMessageBox(error.empty() ? _("Choose a boat with polars first.") : error);
    return;
  }
  wxDialog dialog(this, wxID_ANY, _("Boat and polar details"),
                  wxDefaultPosition, wxSize(660, 590),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       _("Optional information describing these polars. Leave unknown "
         "provenance blank. "
         "These descriptions do not certify the data or change the performance "
         "table."));
  auto* grid = new wxFlexGridSizer(2, 8, 8);
  grid->AddGrowableCol(1);
  const auto field = [&](const wxString& label, const wxString& value) {
    grid->Add(new wxStaticText(&dialog, wxID_ANY, label), 0,
              wxALIGN_CENTER_VERTICAL);
    auto* control = new wxTextCtrl(&dialog, wxID_ANY, value);
    control->SetName(label);
    grid->Add(control, 1, wxEXPAND);
    return control;
  };
  auto* name = field(_("Boat name"), boat.Name);
  auto* description = field(_("Boat description"), boat.Description);
  grid->Add(new wxStaticText(&dialog, wxID_ANY, _("Polar")), 0,
            wxALIGN_CENTER_VERTICAL);
  auto* selected = new wxChoice(&dialog, wxID_ANY);
  for (const auto& p : boat.Polars)
    selected->Append(p.Name.empty() ? wxFileName(p.FileName).GetName()
                                    : p.Name);
  grid->Add(selected, 1, wxEXPAND);
  selected->SetSelection(0);
  auto* polarName = field(_("Polar name"), "");
  auto* source = field(_("Source / provenance"), "");
  source->SetToolTip(
      _("For example: published manufacturer data, measured trials, an "
        "estimate, or imported data of unknown origin."));
  auto* sails = field(_("Sail configuration"), "");
  auto* notes = field(_("Notes"), "");
  auto* modified = new wxStaticText(&dialog, wxID_ANY, "");
  s->Add(grid, 1, wxEXPAND | wxALL, 12);
  s->Add(modified, 0, wxEXPAND | wxALL, 12);
  int current = 0;
  const auto store = [&] {
    auto& p = boat.Polars.at(current);
    if (p.Name != polarName->GetValue() || p.Source != source->GetValue() ||
        p.SailConfiguration != sails->GetValue() ||
        p.Notes != notes->GetValue())
      p.Modified =
          wxDateTime::Now().Format("%Y-%m-%dT%H:%M:%SZ", wxDateTime::UTC);
    p.Name = polarName->GetValue();
    p.Source = source->GetValue();
    p.SailConfiguration = sails->GetValue();
    p.Notes = notes->GetValue();
  };
  const auto load = [&] {
    const auto& p = boat.Polars.at(current);
    polarName->SetValue(p.Name);
    source->SetValue(p.Source);
    sails->SetValue(p.SailConfiguration);
    notes->SetValue(p.Notes);
    modified->SetLabel(_("Metadata modified: ") +
                       (p.Modified.empty() ? _("unknown") : p.Modified));
  };
  selected->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) {
    store();
    current = selected->GetSelection();
    load();
  });
  s->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
  dialog.FindWindow(wxID_OK)->SetLabel(_("Save"));
  dialog.SetSizer(s);
  load();
  dialog.CentreOnParent();
  if (dialog.ShowModal() != wxID_OK) return;
  store();
  boat.Name = name->GetValue();
  boat.Description = description->GetValue();
  const auto saved = boat.SaveXML(path);
  if (!saved.empty()) {
    wxMessageBox(saved, _("Cannot save boat details"), wxOK, this);
    return;
  }
  ApplyBoat(path);
}

void PassagePanel::LoadWeather() {
  wxFileDialog dialog(
      this, _("Load passage weather"), wxEmptyString, wxEmptyString,
      _("GRIB files|*.grb;*.grib;*.grb2;*.grib2;*.gz;*.bz2|All files|*.*"),
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dialog.ShowModal() != wxID_OK) return;
  wxString error;
  if (!routing_.OpenPassageForecast(dialog.GetPath(), error))
    wxMessageBox(error, _("Cannot load weather"), wxOK | wxICON_INFORMATION,
                 this);
  RefreshPassage();
}
void PassagePanel::GenerateWeather() {
  EnsurePassage();
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  if (!generator_)
    generator_ =
        new EnvironmentalGribDialog(this, [this](const wxString& path) {
          wxString error;
          if (!routing_.OpenPassageForecast(path, error))
            wxMessageBox(error, _("Generated weather"), wxOK, this);
          RefreshPassage();
        });
  const auto c = PlanningConfiguration(r->GetConfiguration());
  if (!generator_->IsShown() && std::isfinite(c.StartLat) &&
      std::isfinite(c.EndLat)) {
    std::vector<RouteWaypointInfo> points;
    wxString error;
    const auto guid =
        c.IsMultiLegGenerated ? c.MultiLegParentRouteGUID : c.RouteGUID;
    if (!guid.empty()) ExtractOpenCPNRouteWaypoints(guid, points, error);
    if (points.size() < 2)
      points = {{c.StartGUID, c.Start, c.StartLat, c.StartLon, 0},
                {c.EndGUID, c.End, c.EndLat, c.EndLon, 1}};
    double distance = 0, south = points.front().lat, north = south;
    double west = points.front().lon, east = west, previous = west;
    for (size_t i = 0; i < points.size(); ++i) {
      const auto& p = points[i];
      double lon = p.lon;
      while (lon - previous > 180) lon -= 360;
      while (lon - previous < -180) lon += 360;
      west = std::min(west, lon);
      east = std::max(east, lon);
      previous = lon;
      south = std::min(south, p.lat);
      north = std::max(north, p.lat);
      if (i)
        distance += DistGreatCircle_Plugin(points[i - 1].lat, points[i - 1].lon,
                                           p.lat, p.lon);
    }
    // Preliminary duration at 4 kn plus 50% and 24 h; actual coverage is
    // rechecked.
    const int estimate =
        std::max(24, int(std::ceil((distance / 4 * 1.5 + 24) / 3)) * 3);
    const int hours = std::min(
        360, c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                 ? std::max(estimate, c.ArrivalSearchHorizonMinutes / 60 + 24)
                 : estimate);
    const double margin = std::max(2.0, std::min(10.0, distance / 120));
    auto wrap = [](double lon) {
      while (lon > 180) lon -= 360;
      while (lon < -180) lon += 360;
      return lon;
    };
    const bool global = east - west + 2 * margin >= 359;
    const wxDateTime start =
        c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
            ? c.PlannedArrivalTime -
                  wxTimeSpan::Minutes(c.ArrivalSearchHorizonMinutes)
            : c.StartTime;
    generator_->SetPassage(
        global ? -180 : wrap(west - margin), std::max(-89.0, south - margin),
        global ? 180 : wrap(east + margin), std::min(89.0, north + margin),
        start, hours, c.Currents, c.MaxSwellMeters > 0);
  }
  generator_->Show();
  generator_->Raise();
}
