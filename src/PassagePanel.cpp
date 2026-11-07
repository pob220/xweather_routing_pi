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
    : wxScrolledWindow(parent, wxID_ANY), routing_(routing), refresh_(this) {
  SetScrollRate(0, 14);
  auto* page = new wxBoxSizer(wxVERTICAL);
  auto* title = new wxBoxSizer(wxHORIZONTAL);
  Text(this, title, _("Weather Routing"), true);
  Text(this, title, "2.0.0");
  page->Add(title, 0, wxEXPAND | wxALL, 7);
  auto* plans = new wxBoxSizer(wxHORIZONTAL);
  passages_ = new wxChoice(this, wxID_ANY);
  plans->Add(passages_, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5);
  auto* add = new wxButton(this, wxID_ANY, _("New passage"));
  plans->Add(add, 0, wxALL, 5);
  add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto c = routing_.DefaultConfiguration();
    c.Start.clear();
    c.End.clear();
    c.StartLat = c.StartLon = c.EndLat = c.EndLon = NAN;
    routing_.AddConfiguration(c);
    SelectPassage(int(routing_.m_WeatherRoutes.size()) - 1);
    EditRoute();
  });
  passages_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    SelectPassage(passages_->GetSelection());
  });
  page->Add(plans, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

  auto card = [&](const wxString& label) {
    auto* box = new wxStaticBoxSizer(wxVERTICAL, this, label);
    box->GetStaticBox()->SetFont(box->GetStaticBox()->GetFont().Bold());
    page->Add(box, 0, wxEXPAND | wxALL, 10);
    return box;
  };
  auto* route = card(_("1. Route"));
  route_ =
      Text(this, route, _("Choose your start, destination and passage time."));
  auto* routeActions = new wxBoxSizer(wxHORIZONTAL);
  edit_ = Button(this, routeActions, _("Edit route"), [this] { EditRoute(); });
  Button(this, routeActions, _("Show on chart"), [this] {
    wxCommandEvent e;
    routing_.OnGoTo(e);
    routing_.Hide();
  });
  route->Add(routeActions, 0, wxEXPAND);
  auto* boat = card(_("2. Boat and polars"));
  boat_ = Text(this, boat, "");
  auto* boatActions = new wxBoxSizer(wxHORIZONTAL);
  library_ = Button(this, boatActions, _("Choose from polar library"),
                    [this] { ShowLibrary(); });
  Button(this, boatActions, _("Import boat / polar"), [this] { ChooseBoat(); });
  Button(this, boatActions, _("Edit boat and polars"), [this] {
    EnsurePassage();
    routing_.m_ConfigurationDialog.EditBoat();
  });
  boat->Add(boatActions, 0, wxEXPAND);
  auto* weather = card(_("3. Weather"));
  weather_ = Text(this, weather, "");
  auto* weatherActions = new wxBoxSizer(wxHORIZONTAL);
  load_ =
      Button(this, weatherActions, _("Load GRIB"), [this] { LoadWeather(); });
  Button(this, weatherActions, _("Download / generate GRIB"),
         [this] { GenerateWeather(); });
  weather->Add(weatherActions, 0, wxEXPAND);
  auto* sourceActions = new wxBoxSizer(wxHORIZONTAL);
  Button(this, sourceActions, _("Use Climatology"), [this] {
    EnsurePassage();
    auto* r = routing_.FirstCurrentRouteMap();
    if (!r) return;
    auto c = r->GetConfiguration();
    c.UseGrib = false;
    c.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
    routing_.ApplyPassageConfiguration(c);
    RefreshPassage();
  });
  Button(this, sourceActions, _("Forecast + Climatology"), [this] {
    EnsurePassage();
    auto* r = routing_.FirstCurrentRouteMap();
    if (!r) return;
    auto c = r->GetConfiguration();
    c.UseGrib = true;
    c.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
    routing_.ApplyPassageConfiguration(c);
    RefreshPassage();
  });
  weather->Add(sourceActions, 0, wxEXPAND);
  auto* settings = new wxBoxSizer(wxHORIZONTAL);
  Button(this, settings, _("Basic settings"), [this] {
    EnsurePassage();
    routing_.m_ConfigurationDialog.ShowSettingsSection(0);
    routing_.OnEditConfiguration();
  });
  Button(this, settings, _("Advanced settings"), [this] {
    EnsurePassage();
    routing_.m_ConfigurationDialog.ShowSettingsSection(1);
    routing_.OnEditConfiguration();
  });
  page->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  limits_ = Text(this, page, "");
  status_ = Text(this, page, "");
  auto* compute = new wxBoxSizer(wxHORIZONTAL);
  calculate_ = Button(this, compute, _("Calculate Route"), [this] {
    auto* r = routing_.FirstCurrentRouteMap();
    if (!r) return;
    wxCommandEvent e;
    if (routing_.RouteComputationActive(r))
      routing_.OnStop(e);
    else if (r->GetConfiguration().IsMultiLegGenerated)
      routing_.OnComputeMultiLegSequence(e);
    else
      routing_.OnCompute(e);
    RefreshPassage();
  });
  calculate_->SetFont(calculate_->GetFont().Bold());
  calculate_->SetMinSize(wxSize(-1, 42));
  Button(this, compute, _("Results and report"), [this] {
    wxCommandEvent e;
    routing_.OnReport(e);
  });
  page->Add(compute, 0, wxEXPAND | wxALL, 10);
  SetSizer(page);
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
void PassagePanel::RefreshPassage() {
  wxArrayString names;
  int selected = -1, index = 0;
  auto* current = routing_.FirstCurrentRouteMap();
  for (auto* item : routing_.m_WeatherRoutes) {
    const auto c = item->routemapoverlay->GetConfiguration();
    names.Add(c.Start.empty() ? _("New passage") : c.Start + " to " + c.End);
    if (item->routemapoverlay == current) selected = index;
    ++index;
  }
  if (passages_->GetStrings() != names) passages_->Set(names);
  if (selected >= 0) passages_->SetSelection(selected);
  if (!current) return;
  const auto c = current->GetConfiguration();
  const bool running = routing_.RouteComputationActive(current);
  const bool positions = !c.Start.empty() && !c.End.empty() &&
                         std::isfinite(c.StartLat) &&
                         std::isfinite(c.StartLon) && std::isfinite(c.EndLat) &&
                         std::isfinite(c.EndLon);
  wxString when =
      c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
          ? _("Target arrival: ") +
                c.PlannedArrivalTime.Format("%d %b %Y, %H:%M", wxDateTime::UTC)
          : _("Departure: ") +
                c.StartTime.Format("%d %b %Y, %H:%M", wxDateTime::UTC);
  wxString route = positions ? c.Start + "  →  " + c.End + "\n" + when + " UTC"
                             : _("Choose your start, destination and departure "
                                 "or target arrival time.");
  if (c.IsMultiLegGenerated)
    route += wxString::Format(
        _("\nPassage leg %d of %d. Calculate runs the legs in sequence."),
        c.MultiLegLegIndex, c.MultiLegLegCount);
  route_->SetLabel(route);
  const wxString boatError = boatModel_.OpenXML(c.boatFileName, true);
  const bool boatReady = boatError.empty() && !boatModel_.Polars.empty();
  bool example =
      wxFileName(c.boatFileName).GetName() == "Example-40ft-monohull";
  for (const auto& polar : boatModel_.Polars)
    example = example || polar.FileName.Contains("40ft-monohull-example.pol");
  boat_->SetLabel(
      boatReady
          ? (example ? _("Example 40 ft monohull — illustrative performance, "
                         "not your vessel")
                     : DisplayName(wxFileName(c.boatFileName).GetName())) +
                wxString::Format(_("\n%d polar configuration(s)."),
                                 int(boatModel_.Polars.size()))
          : _("Choose or import a usable boat and polar.") + "\n" + boatError);
  auto& provider = routing_.PassageForecast();
  const bool climate = c.ClimatologyType > RouteMapConfiguration::CURRENTS_ONLY;
  wxString weather =
      c.UseGrib
          ? provider.Summary() + "\n" + provider.Details()
          : _("Climatology planning — typical conditions, not a forecast");
  if (climate)
    weather += "\n" + weather_routing::BundledClimatology::Source() +
               (c.UseGrib ? _(" — available beyond forecast coverage") : "");
  bool wind = c.UseGrib && provider.Loaded();
  if (wind && positions &&
      c.TimeMode == RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME) {
    wind = provider.HasWind(c.StartTime.GetTicks(), c.StartLat, c.StartLon);
    if (!wind)
      weather +=
          _("\nForecast wind does not cover the departure position and time.");
    else
      weather +=
          _("\nDeparture wind is available. Coverage is checked throughout "
            "calculation.");
  }
  if (!c.UseGrib && !climate)
    weather =
        _("No weather source selected. Load a GRIB or choose Climatology.");
  bool climateReady = climate;
  if (climate && positions && !wind && !running &&
      c.TimeMode == RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME) {
    std::lock_guard<std::recursive_mutex> lock(
        ClimatologyThreadGuard::InvocationMutex());
    double direction = 0, speed = 0;
    const bool vector = RouteMap::ClimatologyData &&
                        RouteMap::ClimatologyData(0, c.StartTime, c.StartLat,
                                                  c.StartLon, direction, speed);
    climateReady = vector;
    if (c.ClimatologyType > RouteMapConfiguration::AVERAGE) {
      int count = 8;
      double frequencies[8]{}, speeds[8]{}, gale = 0, calm = 0;
      climateReady = RouteMap::ClimatologyWindAtlasData &&
                     RouteMap::ClimatologyWindAtlasData(
                         c.StartTime, c.StartLat, c.StartLon, count,
                         frequencies, speeds, gale, calm);
    }
    if (!climateReady)
      weather +=
          _("\nClimatology has no usable wind at the departure position. Load "
            "a forecast or review the position.");
  }
  weather_->SetLabel(weather);
  limits_->SetLabel(wxString::Format(
      _("Review passage limits: wind %.0f kn · waves %.1f m · %s · %s"),
      c.MaxTrueWindKnots, c.MaxSwellMeters,
      c.Currents ? _("currents enabled") : _("currents off"),
      c.DetectLand ? _("land avoidance on") : _("land avoidance off")));
  const bool ready = positions && boatReady && (wind || climateReady);
  const bool stale = current->Finished() && c.UseGrib && provider.Loaded() &&
                     !current->GetDiagnostics().context.Contains(
                         wxString::FromUTF8(provider.Identity()));
  status_->SetLabel(
      running ? _("Calculating passage… You can cancel and keep your setup.")
      : !positions             ? _("Define the passage to begin.")
      : !boatReady             ? _("Choose a valid boat to continue.")
      : !wind && !climateReady ? _("Select usable weather to continue.")
      : stale ? _("Weather has changed since this result. Recalculate to use "
                  "the selected forecast.")
      : current->Finished() ? current->GetDiagnostics().Summary()
                            : _("Ready to calculate. Review the example boat "
                                "and operating limits for your passage."));
  calculate_->SetLabel(running ? _("Cancel calculation")
                               : _("Calculate Route"));
  calculate_->Enable(running || ready);
  for (auto* b : {edit_, library_, load_}) b->Enable(!running);
  // Wrap against the actual viewport; no fixed desktop-sized minimum.
  for (auto* t : {route_, boat_, weather_, limits_, status_})
    t->Wrap(std::max(220, GetClientSize().x - 64));
  Layout();
  FitInside();
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
      name[i]->Append(p.Name + (p.GUID.empty() ? "" : _(" (waypoint)")));
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
  wxArrayString files;
  wxDir::GetAllFiles(weather_routing_pi::StandardPath() + "polars", &files,
                     "*.pol", wxDIR_FILES | wxDIR_DIRS);
  files.Sort();
  wxDialog dialog(this, wxID_ANY, _("Polar library"), wxDefaultPosition,
                  wxSize(600, 570), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       _("Choose a library polar as a starting point for your boat. Check the "
         "data against your vessel before passage planning."));
  auto* search = new wxSearchCtrl(&dialog, wxID_ANY);
  s->Add(search, 0, wxEXPAND | wxALL, 12);
  auto* list = new wxListBox(&dialog, wxID_ANY);
  s->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);
  std::vector<wxString> filtered;
  auto filter = [&] {
    filtered.clear();
    list->Clear();
    for (const auto& f : files)
      if (DisplayName(wxFileName(f).GetName())
              .Lower()
              .Contains(search->GetValue().Lower())) {
        filtered.push_back(f);
        list->Append(DisplayName(wxFileName(f).GetName()));
      }
    if (list->GetCount()) list->SetSelection(0);
  };
  filter();
  search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { filter(); });
  s->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
  dialog.SetSizer(s);
  dialog.CentreOnParent();
  if (dialog.ShowModal() != wxID_OK || list->GetSelection() < 0) return;
  const wxString polar = filtered.at(list->GetSelection());
  Boat boat;
  Polar p;
  wxString error;
  if (!p.Open(polar, error)) {
    wxMessageBox(error);
    return;
  }
  boat.Polars.push_back(p);
  const wxString path =
      weather_routing_pi::StandardPath() + "boats/" +
      wxString::Format("boat-%lld.xml", wxGetUTCTimeMillis().GetValue());
  error = boat.SaveXML(path);
  if (!error.empty()) {
    wxMessageBox(error);
    return;
  }
  ApplyBoat(path);
  routing_.m_ConfigurationDialog.EditBoat();
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
  const auto c = r->GetConfiguration();
  if (!generator_->IsShown() && std::isfinite(c.StartLat) &&
      std::isfinite(c.EndLat)) {
    const double distance =
        DistGreatCircle_Plugin(c.StartLat, c.StartLon, c.EndLat, c.EndLon);
    // Preliminary duration at 4 kn plus 50% and 24 h; actual coverage is
    // rechecked.
    const int estimate =
        std::max(24, int(std::ceil((distance / 4 * 1.5 + 24) / 3)) * 3);
    const int hours = std::min(
        384, c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                 ? std::max(estimate, c.ArrivalSearchHorizonMinutes / 60 + 24)
                 : estimate);
    const double margin = std::max(2.0, std::min(10.0, distance / 120));
    auto wrap = [](double lon) {
      while (lon > 180) lon -= 360;
      while (lon < -180) lon += 360;
      return lon;
    };
    double end = c.EndLon;
    while (end - c.StartLon > 180) end -= 360;
    while (end - c.StartLon < -180) end += 360;
    const wxDateTime start =
        c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
            ? c.PlannedArrivalTime -
                  wxTimeSpan::Minutes(c.ArrivalSearchHorizonMinutes)
            : c.StartTime;
    generator_->SetPassage(
        wrap(std::min(c.StartLon, end) - margin),
        std::max(-89.0, std::min(c.StartLat, c.EndLat) - margin),
        wrap(std::max(c.StartLon, end) + margin),
        std::min(89.0, std::max(c.StartLat, c.EndLat) + margin), start, hours,
        c.Currents, c.MaxSwellMeters > 0);
  }
  generator_->Show();
  generator_->Raise();
}
