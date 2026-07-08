/***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
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
 ***************************************************************************/

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/imaglist.h>
#include <wx/progdlg.h>
#include <wx/dir.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <wx/glcanvas.h>

#include "tinyxml.h"

#include "Utilities.h"
#include "Boat.h"
#include "BoatDialog.h"
#include "RouteMapOverlay.h"
#include "RouteWaypointExtractor.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"
#include "AboutDialog.h"
#include "ConstraintChecker.h"
#include "icons.h"
#include "navobj_util.h"
#include "ocpn_plugin.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

static const int MAX_DEPARTURE_OPTIMIZATION_CANDIDATES = 73;
static bool s_loggedDetectLandGshhsWarning = false;

static void ReadExperimentalChartSafetySettings(bool& use_chart_safety,
                                                bool& enforce_chart_safety) {
  use_chart_safety = false;
  enforce_chart_safety = false;
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  pConf->Read(_T("UseExperimentalChartSafety"), &use_chart_safety, false);
  pConf->Read(_T("EnforceExperimentalChartSafety"), &enforce_chart_safety,
              false);
}

static double ReadExperimentalChartSafetyPrewarmMarginNm() {
  double margin_nm = 10.0;
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  pConf->Read(_T("ExperimentalChartSafetyPrewarmMarginNm"), &margin_nm,
              10.0);
  if (!std::isfinite(margin_nm)) margin_nm = 10.0;
  return wxMax(0.0, wxMin(60.0, margin_nm));
}

static void PrewarmExperimentalChartSafetyForConfiguration(
    const RouteMapConfiguration& configuration, const wxString& context) {
  if (!configuration.DetectLand) return;

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  if (!use_chart_safety) return;

  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  if (!PlugIn_PrewarmSegmentSafetyGridForSegment(
          configuration.StartLat, configuration.StartLon, configuration.EndLat,
          configuration.EndLon, configuration.SafetyMarginLand, &result)) {
    wxLogMessage(
        "WeatherRouting Detect Land: chart safety prewarm failed context=%s "
        "route=\"%s to %s\".",
        context, configuration.Start, configuration.End);
    return;
  }

  wxString message = wxString::Format(
      "WeatherRouting Detect Land: chart safety prewarm context=%s "
      "route=\"%s to %s\" start=(%.6f,%.6f) end=(%.6f,%.6f) "
      "margin_nm=%.3f enforce=%d ",
      context, configuration.Start, configuration.End, configuration.StartLat,
      configuration.StartLon, configuration.EndLat, configuration.EndLon,
      configuration.SafetyMarginLand, enforce_chart_safety ? 1 : 0);
  message += wxString::Format(
      "tile_builds=%d tile_hits=%d build_ms=%d cells=%d land=%d water=%d "
      "drying=%d unknown=%d point_queries=%d point_cache_hits=%d "
      "grid_cache_size=%d grid_cache_evictions=%d.",
      result.grid_cache_misses, result.grid_cache_hits, result.grid_build_ms,
      result.grid_cells_total, result.grid_cells_land,
      result.grid_cells_water, result.grid_cells_drying,
      result.grid_cells_unknown, result.point_cache_misses,
      result.point_cache_hits, result.grid_cache_size,
      result.grid_cache_evictions);
  wxLogMessage("%s", message.c_str());
}

static void PrewarmExperimentalChartSafetyForMultiLegEnvelope(
    const std::vector<RouteMapOverlay*>& routes, const wxString& context) {
  if (routes.empty()) return;

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  if (!use_chart_safety) return;

  bool any_detect_land = false;
  double max_safety_margin_nm = 0.0;
  for (auto route : routes) {
    if (!route) continue;
    RouteMapConfiguration configuration = route->GetConfiguration();
    if (!configuration.DetectLand) continue;
    any_detect_land = true;
    max_safety_margin_nm =
        wxMax(max_safety_margin_nm, configuration.SafetyMarginLand);
  }
  if (!any_detect_land) return;

  double envelope_margin_nm =
      ReadExperimentalChartSafetyPrewarmMarginNm() + max_safety_margin_nm;

  wxStopWatch timer;
  int legs = 0;
  int failed = 0;
  int tile_builds = 0;
  int tile_hits = 0;
  int build_ms = 0;
  int cells = 0;
  int land = 0;
  int water = 0;
  int drying = 0;
  int unknown = 0;
  int point_queries = 0;
  int point_cache_hits = 0;
  int grid_cache_size = 0;
  int grid_cache_evictions = 0;
  bool capped = false;

  for (auto route : routes) {
    if (!route) continue;
    RouteMapConfiguration configuration = route->GetConfiguration();
    if (!configuration.DetectLand) continue;
    ++legs;

    PlugInSegmentSafetyResult result = {};
    result.struct_size = sizeof(result);
    if (!PlugIn_PrewarmSegmentSafetyGridForSegment(
            configuration.StartLat, configuration.StartLon,
            configuration.EndLat, configuration.EndLon, envelope_margin_nm,
            &result)) {
      ++failed;
      continue;
    }

    tile_builds += result.grid_cache_misses;
    tile_hits += result.grid_cache_hits;
    build_ms += result.grid_build_ms;
    cells += result.grid_cells_total;
    land += result.grid_cells_land;
    water += result.grid_cells_water;
    drying += result.grid_cells_drying;
    unknown += result.grid_cells_unknown;
    point_queries += result.point_cache_misses;
    point_cache_hits += result.point_cache_hits;
    grid_cache_size = wxMax(grid_cache_size, result.grid_cache_size);
    grid_cache_evictions =
        wxMax(grid_cache_evictions, result.grid_cache_evictions);
    capped = capped || wxString(result.message).Find("capped") != wxNOT_FOUND;
  }

  wxString message = wxString::Format(
      "WeatherRouting Detect Land: chart safety optimisation corridor "
      "prewarm context=%s legs=%d failed=%d margin_nm=%.3f enforce=%d "
      "elapsed_ms=%ld capped=%d ",
      context, legs, failed, envelope_margin_nm, enforce_chart_safety ? 1 : 0,
      timer.Time(), capped ? 1 : 0);
  message += wxString::Format(
      "tile_builds=%d tile_hits=%d build_ms=%d cells=%d land=%d water=%d "
      "drying=%d unknown=%d point_queries=%d point_cache_hits=%d "
      "grid_cache_size=%d grid_cache_evictions=%d.",
      tile_builds, tile_hits, build_ms, cells, land, water, drying, unknown,
      point_queries, point_cache_hits, grid_cache_size, grid_cache_evictions);
  wxLogMessage("%s", message.c_str());
}

wxString GetRouteNameForGuid(const wxString& routeGuid) {
  std::unique_ptr<PlugIn_Route> route = GetRoute_Plugin(routeGuid);
  if (route && !route->m_NameString.IsEmpty()) return route->m_NameString;
  return routeGuid;
}

/**
 * Used for NEflag argument to toSDMM_Plugin function from ocpn_plugin.h.
 * @todo Should probably be declared there instead, and probably be a boolean.
 */
enum NEflag {
  LAT = 1,
  LON = 2,
};

/**
 * Used for precision argument to toSDMM_Plugin function from ocpn_plugin.h.
 **/
enum Precision {
  LO = 0,
  HI = 1,
};

/* XPM */
static const char* eye[] = {"20 20 7 1",
                            ". c none",
                            "# c #000000",
                            "a c #333333",
                            "b c #666666",
                            "c c #999999",
                            "d c #cccccc",
                            "e c #ffffff",
                            "....................",
                            "....................",
                            "....................",
                            "....................",
                            ".......######.......",
                            ".....#aabccb#a#.....",
                            "....#deeeddeebcb#...",
                            "..#aeeeec##aceaec#..",
                            ".#bedaeee####dbcec#.",
                            "#aeedbdabc###bcceea#",
                            ".#bedad######abcec#.",
                            "..#be#d######dadb#..",
                            "...#abac####abba#...",
                            ".....##acbaca##.....",
                            ".......######.......",
                            "....................",
                            "....................",
                            "....................",
                            "....................",
                            "...................."};

WeatherRoute::WeatherRoute() : routemapoverlay(new RouteMapOverlay) {}
WeatherRoute::~WeatherRoute() { delete routemapoverlay; }

const wxString WeatherRouting::column_names[NUM_COLS] = {_("Visible"),
                                                         _("Boat"),
                                                         _("Start Type"),
                                                         _("Start"),
                                                         _("Start Time"),
                                                         _("End"),
                                                         _("End Time"),
                                                         _("Time"),
                                                         _("Distance"),
                                                         _("Avg Speed"),
                                                         _("Max Speed"),
                                                         _("Avg Speed Ground"),
                                                         _("Max Speed Ground"),
                                                         _("Avg Wind"),
                                                         _("Max Wind"),
                                                         _("Max Wind Gust"),
                                                         _("Avg Current"),
                                                         _("Max Current"),
                                                         _("Avg Swell"),
                                                         _("Max Swell"),
                                                         _("Upwind Percentage"),
                                                         _("Port Starboard"),
                                                         _("Tacks"),
                                                         _("Jibes"),
                                                         _("Sail Plan Changes"),
                                                         _("Comfort"),
                                                         _("State")};

static int sortcol, sortorder = 1;
// sort callback. Sort by body.
#if wxCHECK_VERSION(2, 9, 0)
int wxCALLBACK SortWeatherRoutes(wxIntPtr item1, wxIntPtr item2, wxIntPtr list)
#else
int wxCALLBACK SortWeatherRoutes(long item1, long item2, long list)
#endif
{
  wxListCtrl* lc = (wxListCtrl*)list;

  wxListItem it1, it2;

  it1.SetId(lc->FindItem(-1, item1));
  it1.SetColumn(sortcol);

  it2.SetId(lc->FindItem(-1, item2));
  it2.SetColumn(sortcol);

  lc->GetItem(it1);
  lc->GetItem(it2);

  return sortorder * it1.GetText().Cmp(it2.GetText());
}

WeatherRouting::WeatherRouting(wxWindow* parent, weather_routing_pi& plugin)
    : WeatherRoutingBase(parent),
      m_panel(NULL),
      m_ConfigurationDialog(*this),
      m_ConfigurationBatchDialog(this),
      m_CursorPositionDialog(this),
      // CUSTOMIZATION
      m_RoutePositionDialog(this),
      m_BoatDialog(*this),
      m_SettingsDialog(this),
      m_StatisticsDialog(this),
      m_ReportDialog(*this),
      m_PlotDialog(*this),
      m_FilterRoutesDialog(this),
      m_bRunning(false),
      m_RoutesToRun(0),
      m_bSkipUpdateCurrentItems(false),
      m_ActiveMultiLegCurrentLegIndex(0),
      m_ActiveMultiLegSequence(false),
      m_ApplyingMultiLegGroupSettings(false),
      m_ActiveMultiLegOptimizationCandidateIndex(-1),
      m_ActiveMultiLegOptimizationLegIndex(0),
      m_AppliedMultiLegOptimizationCandidateIndex(-1),
      m_ActiveMultiLegDepartureOptimization(false),
      m_bShowConfiguration(false),
      m_bShowConfigurationBatch(false),
      m_bShowRoutePosition(false),
      m_bShowSettings(false),
      m_bShowStatistics(false),
      m_bShowReport(false),
      m_bShowPlot(false),
      m_bShowFilter(false),
      m_weather_routing_pi(plugin),
      m_positionOnRoute(nullptr),
      m_RoutingTablePanel(nullptr) {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/Plugins/WeatherRouting" ));

  wxIcon icon;
  icon.CopyFromBitmap(*_img_WeatherRouting);
  m_ConfigurationDialog.SetIcon(icon);
  m_ConfigurationBatchDialog.SetIcon(icon);
  m_BoatDialog.SetIcon(icon);
  m_SettingsDialog.SetIcon(icon);
  m_StatisticsDialog.SetIcon(icon);
  m_ReportDialog.SetIcon(icon);
  m_PlotDialog.SetIcon(icon);
  m_FilterRoutesDialog.SetIcon(icon);

  m_default_configuration_path = weather_routing_pi::StandardPath() +
                                 _T("WeatherRoutingConfiguration.xml");

  bool forceCopyBoats = false;
  bool forceCopyPolars = false;
  wxString boatsdir = weather_routing_pi::StandardPath() +
                      wxFileName::GetPathSeparator() + _T("boats");
  if (!wxFileName::DirExists(boatsdir)) forceCopyBoats = true;
  wxString polarsdir = weather_routing_pi::StandardPath() +
                       wxFileName::GetPathSeparator() + _T("polars");
  if (!wxFileName::DirExists(polarsdir)) forceCopyPolars = true;

  /* ensure the directories exist */
  wxFileName fn;
  fn.Mkdir(weather_routing_pi::StandardPath(), wxS_DIR_DEFAULT,
           wxPATH_MKDIR_FULL);
  fn.Mkdir(boatsdir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  fn.Mkdir(polarsdir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  /* if the boats or polars directories did not previously exist, populate them
   */
  if (forceCopyBoats)
    CopyDataFiles(GetPluginDataDir("weather_routing_pi") + _T("/data/boats"),
                  boatsdir);
  if (forceCopyPolars)
    CopyDataFiles(GetPluginDataDir("weather_routing_pi") + _T("/data/polars"),
                  polarsdir);

  int confVersion;
  pConf->Read(_T ( "ConfigVersion" ), &confVersion, 0);

#ifndef __OCPN__ANDROID__
  if (confVersion < PLUGIN_VERSION_MAJOR * 100 + PLUGIN_VERSION_MINOR) {
    wxString title = _("New or updated data available");
    wxString message =
        _("A new version of the Weather Route plugin has been installed.\n\n"
          "\"Import new boats and polars\" will overwrite the standard boats\n"
          "and polars with newer data. If you have modified this data and not\n"
          "changed the names, your modifications will be overwritten, so be\n"
          "sure to backup your changes. If you have added new polars or boats\n"
          "with exclusive names, they will be kept untouched.\n\n");
    message += _(
        "Import example configurations will overwrite your route\n"
        "configurations with a sample set showing you how WeatherRouting\n"
        "works. Backup your existing configurations if you need.\n\n"
        "Pressing \"OK\" will apply the selected changes, pressing \"Cancel\"\n"
        "will do nothing and you will be asked again on the next launch.");

    wxString confDlgChoices[3] = {_("Import new boats and polars"),
                                  _("Import example configurations")};

    wxMultiChoiceDialog confDlg(this, message, title, 2, confDlgChoices);
    /* check on by default if user starts WR for the first time */
    if (confVersion == 0) {
      wxArrayInt sel;
      sel.Add(0);
      sel.Add(1);
      confDlg.SetSelections(sel);
    }

    if (confDlg.ShowModal() == wxID_OK) {
      wxArrayInt result = confDlg.GetSelections();
      for (size_t i = 0; i < result.GetCount(); i++) {
        if (result[i] == 0) {
          CopyDataFiles(
              GetPluginDataDir("weather_routing_pi") + _T("/data/boats"),
              boatsdir);
          CopyDataFiles(
              GetPluginDataDir("weather_routing_pi") + _T("/data/polars"),
              polarsdir);
        } else if (result[i] == 1) {
          wxString cfg = GetPluginDataDir("weather_routing_pi") + _T("/data/") +
                         _T("WeatherRoutingConfiguration.xml");
          if (wxFileName::FileExists(cfg))
            wxCopyFile(cfg, m_default_configuration_path);
        }
      }
      pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));  // path can change after
                                                        // modal dialog
      pConf->Write(_T ( "ConfigVersion" ),
                   PLUGIN_VERSION_MAJOR * 100 + PLUGIN_VERSION_MINOR);
    }
  }
#endif
  m_SettingsDialog.LoadSettings();

  pConf->SetPath(
      _T( "/PlugIns/WeatherRouting" ));  // path can change after modal dialog
  pConf->Read(_T ( "DisableColPane" ), &m_disable_colpane, false);
#ifdef __OCPN__ANDROID__
  m_disable_colpane = true;
#endif

  wxBoxSizer* bSizer;
  bSizer = new wxBoxSizer(wxVERTICAL);
  this->SetSizer(bSizer);
  if (!m_disable_colpane) {
    m_colpane = new wxCollapsiblePane(this, wxID_ANY, _("Weather Routing"),
                                      wxDefaultPosition, wxDefaultSize,
                                      wxCP_NO_TLW_RESIZE);
    bSizer->Add(m_colpane, 1, wxEXPAND | wxALL, 5);
    m_colpaneWindow = m_colpane->GetPane();
    wxSizer* paneSz = new wxBoxSizer(wxVERTICAL);
    m_colpaneWindow->SetSizer(paneSz);
    m_panel = new WeatherRoutingPanel(m_colpaneWindow);
    paneSz->Add(m_panel, 1, wxEXPAND, 0);
    paneSz->SetSizeHints(m_colpaneWindow);
  } else {
    m_colpane = NULL;
    m_colpaneWindow = this;
    m_panel = new WeatherRoutingPanel(m_colpaneWindow);
    bSizer->Add(m_panel, 1, wxEXPAND, 0);
  }
  bSizer->SetSizeHints(this);

  m_panel->m_lPositions->InsertColumn(POSITION_NAME, _("Name"));
  m_panel->m_lPositions->InsertColumn(POSITION_LAT, _("Lat"));
  m_panel->m_lPositions->InsertColumn(POSITION_LON, _("Lon"));

  wxImageList* imglist = new wxImageList(20, 20, true, 1);
  imglist->Add(wxBitmap(eye));
  m_panel->m_lWeatherRoutes->AssignImageList(imglist, wxIMAGE_LIST_SMALL);

  UpdateColumns();

  if (m_colpane) m_colpane->Expand();

  OpenXML(m_default_configuration_path, false);

  wxPoint p = GetPosition();
  pConf->Read(_T ( "DialogX" ), &p.x, p.x);
  pConf->Read(_T ( "DialogY" ), &p.y, p.y);

  m_size = GetSize();
  pConf->Read(_T ( "DialogWidth" ), &m_size.x, wxMax(m_size.x, 100));
  pConf->Read(_T ( "DialogHeight" ), &m_size.y, wxMax(m_size.y, 100));
#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  m_size.x = sz.x * 3 / 5;
  m_size.y = sz.y * 2 / 5;
  int y = 2 * sz.y / 3 - 40;
  if (m_size.y > y) m_size.y = y;
#endif
  SetSize(p.x, p.y, m_size.x, m_size.y);

  pConf->Read(_T ( "DialogSplit" ), &sashpos, 0);

  /* periodically check for updates from computation thread */
  m_tCompute.Connect(wxEVT_TIMER,
                     wxTimerEventHandler(WeatherRouting::OnComputationTimer),
                     NULL, this);

  m_tHideConfiguration.Connect(
      wxEVT_TIMER,
      wxTimerEventHandler(WeatherRouting::OnHideConfigurationTimer), NULL,
      this);

  m_tAutoSaveXML.Connect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnAutoSaveXMLTimer),
      NULL, this);

  Connect(wxEVT_IDLE, wxTimerEventHandler(WeatherRouting::OnRenderedTimer),
          NULL, this);

  SetEnableConfigurationMenu();

  // Connect Events
  if (m_colpane)
    m_colpane->Connect(
        wxEVT_COLLAPSIBLEPANE_CHANGED,
        wxCollapsiblePaneEventHandler(WeatherRouting::OnCollPaneChanged), NULL,
        this);
  // m_panel->m_lPositions->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler(
  // WeatherRouting::OnEditPositionClick ), NULL, this );
  m_panel->m_lPositions->Connect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnPositionKeyDown), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_LEFT_DCLICK,
      wxMouseEventHandler(WeatherRouting::OnEditConfigurationClick), NULL,
      this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_LEFT_DOWN,
      wxMouseEventHandler(WeatherRouting::OnWeatherRoutesListLeftDown), NULL,
      this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_LEFT_UP, wxMouseEventHandler(WeatherRouting::OnLeftUp), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_RIGHT_UP, wxMouseEventHandler(WeatherRouting::OnRightUp), NULL,
      this);
  m_panel->m_lPositions->Connect(
      wxEVT_LEFT_DOWN, wxMouseEventHandler(WeatherRouting::OnLeftDown), NULL,
      this);
  m_panel->m_lPositions->Connect(
      wxEVT_LEFT_UP, wxMouseEventHandler(WeatherRouting::OnLeftUp), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_COL_CLICK,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSort), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_ITEM_DESELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_ITEM_SELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnWeatherRouteKeyDown), NULL, this);
  m_panel->m_bCompute->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
                               wxCommandEventHandler(WeatherRouting::OnCompute),
                               NULL, this);
  m_panel->m_bSaveAsTrack->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsTrack), NULL, this);
  m_panel->m_bSaveAsRoute->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsRoute), NULL, this);
  m_panel->m_bExportRoute->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnExportRouteAsGPX), NULL, this);

#ifdef __OCPN__ANDROID__
  GetHandle()->setAttribute(Qt::WA_AcceptTouchEvents);
  GetHandle()->grabGesture(Qt::PanGesture);
  GetHandle()->setStyleSheet(qtStyleSheet);

  GetHandle()->setAttribute(Qt::WA_AcceptTouchEvents);
  GetHandle()->grabGesture(Qt::PanGesture);
  Connect(
      wxEVT_QT_PANGESTURE,
      (wxObjectEventFunction)(wxEventFunction)&WeatherRouting::OnEvtPanGesture,
      NULL, this);
  m_tDownTimer.Connect(wxEVT_TIMER,
                       wxTimerEventHandler(WeatherRouting::OnDownTimer), NULL,
                       this);
#endif
}

WeatherRouting::~WeatherRouting() {
  // Disconnect Events
  if (m_colpane)
    m_colpane->Disconnect(
        wxEVT_COLLAPSIBLEPANE_CHANGED,
        wxCollapsiblePaneEventHandler(WeatherRouting::OnCollPaneChanged), NULL,
        this);
  // m_panel->m_lPositions->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler(
  // WeatherRouting::OnEditPositionClick ), NULL, this );
  m_panel->m_lPositions->Disconnect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnPositionKeyDown), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_LEFT_DCLICK,
      wxMouseEventHandler(WeatherRouting::OnEditConfigurationClick), NULL,
      this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_LEFT_DOWN,
      wxMouseEventHandler(WeatherRouting::OnWeatherRoutesListLeftDown), NULL,
      this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_LEFT_UP, wxMouseEventHandler(WeatherRouting::OnLeftUp), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_RIGHT_UP, wxMouseEventHandler(WeatherRouting::OnRightUp), NULL,
      this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_COL_CLICK,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSort), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_ITEM_DESELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_ITEM_SELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnWeatherRouteKeyDown), NULL, this);
  m_panel->m_bCompute->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnCompute), NULL, this);
  m_panel->m_bSaveAsTrack->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsTrack), NULL, this);
  m_panel->m_bSaveAsRoute->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsRoute), NULL, this);
  m_panel->m_bExportRoute->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnExportRouteAsGPX), NULL, this);

  m_tAutoSaveXML.Disconnect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnAutoSaveXMLTimer),
      NULL, this);

  StopAll();

  m_SettingsDialog.SaveSettings();

  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  wxPoint p = GetPosition();
  pConf->Write(_T ( "DialogX" ), p.x);
  pConf->Write(_T ( "DialogY" ), p.y);

  pConf->Write(_T ( "DialogWidth" ), m_size.x);
  pConf->Write(_T ( "DialogHeight" ), m_size.y);
  pConf->Write(_T ( "DialogSplit" ), m_panel->m_splitter1->GetSashPosition());

  SaveXML(m_FileName.GetFullPath());

  for (std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
       it != m_WeatherRoutes.end(); it++)
    delete *it;
  delete m_panel;
  delete m_colpane;

  // Clean up routing table panel if it exists
  if (m_RoutingTablePanel) {
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    pauimgr->DetachPane(m_RoutingTablePanel);
    m_RoutingTablePanel->Destroy();
    m_RoutingTablePanel = nullptr;
  }
}

#ifdef __OCPN__ANDROID__
void WeatherRouting::OnEvtPanGesture(wxQT_PanGestureEvent& event) {
  switch (event.GetState()) {
    case GestureStarted:
      m_startPos = GetPosition();
      m_startMouse = event.GetCursorPos();  // g_mouse_pos_screen;
      break;
    default: {
      wxPoint pos = event.GetCursorPos();
      int x = wxMax(0, pos.x + m_startPos.x - m_startMouse.x);
      int y = wxMax(0, pos.y + m_startPos.y - m_startMouse.y);
      int xmax = ::wxGetDisplaySize().x - GetSize().x;
      x = wxMin(x, xmax);
      int ymax =
          ::wxGetDisplaySize().y - GetSize().y;  // Some fluff at the bottom
      y = wxMin(y, ymax);

      Move(x, y);
      m_tDownTimer.Stop();
    } break;
  }
}
#endif

void WeatherRouting::OnLeftDown(wxMouseEvent& event) {
  m_tDownTimer.Start(1200, true);
  m_downPos = event.GetPosition();
  event.Skip();
}

void WeatherRouting::OnLeftUp(wxMouseEvent& event) { m_tDownTimer.Stop(); }

void WeatherRouting::OnDownTimer(wxTimerEvent&) {
  int flags = wxLIST_HITTEST_NOWHERE | wxLIST_HITTEST_ONITEM;
  if (m_panel->m_lWeatherRoutes->HitTest(m_downPos, flags) != wxNOT_FOUND)
    m_panel->m_lWeatherRoutes->PopupMenu(m_mContextMenu, m_downPos);
}

void WeatherRouting::OnRightUp(wxMouseEvent& event) {
  m_panel->m_lWeatherRoutes->PopupMenu(m_mContextMenu, event.GetPosition());
}

void WeatherRouting::CopyDataFiles(wxString from, wxString to) {
  if (from[from.Len() - 1] != '\\' && from[from.Len() - 1] != '/')
    from += wxFILE_SEP_PATH;
  if (to[to.Len() - 1] != '\\' && to[to.Len() - 1] != '/')
    to += wxFILE_SEP_PATH;

  if (!wxDirExists(to))
    wxFileName::Mkdir(to, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  wxDir dir(from);
  wxString next = wxEmptyString;
  bool b = dir.GetFirst(&next);
  while (b) {
    const wxString fileFrom = from + next;
    const wxString fileTo = to + next;
    if (wxDirExists(fileFrom))
      CopyDataFiles(fileFrom, fileTo);
    else {
      wxLogMessage(
          _T("WeatherRouting copy file: " + fileFrom + _T(" to ") + fileTo));
      wxCopyFile(fileFrom, fileTo);
    }
    b = dir.GetNext(&next);
  }
}

void WeatherRouting::Render(piDC& dc, PlugIn_ViewPort& vp) {
  if (vp.bValid == false) return;

  // polling is bad
  bool work = false;
  for (auto& it : RouteMap::Positions) {
    if (it.GUID.IsEmpty()) continue;

    PlugIn_Waypoint waypoint;
    double lat = it.lat;
    double lon = it.lon;

    if (!GetSingleWaypoint(it.GUID, &waypoint)) continue;
    if (lat == waypoint.m_lat && lon == waypoint.m_lon &&
        waypoint.m_MarkName.IsSameAs(it.Name))
      continue;

    long index = m_panel->m_lPositions->FindItem(0, it.ID);
    if (index < 0) {
      // corrupted data
      continue;
    }

    wxString name = waypoint.m_MarkName;
    lat = waypoint.m_lat;
    lon = waypoint.m_lon;
    it.Name = name;
    it.lat = lat;
    it.lon = lon;

    // XXX FIXME there's already this name, update m_ConfigurationDialog source
    m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
    m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);
    m_panel->m_lPositions->SetItem(
        index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
    m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
    m_panel->m_lPositions->SetItem(
        index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
    m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
    work = true;
  }
  if (work) {
    UpdateConfigurations();
    Reset();
  }

  if (!dc.GetDC()) {
#ifndef __OCPN__ANDROID__
    glPushAttrib(GL_LINE_BIT | GL_ENABLE_BIT | GL_HINT_BIT);  // Save state
    glEnable(GL_LINE_SMOOTH);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
#endif
    glEnable(GL_BLEND);
  }

  wxDateTime time = m_ConfigurationDialog.m_GribTimelineTime;
  if (!time.IsValid()) time = wxDateTime::UNow();

  // Update highlighted row in the routing table panel if it exists
  if (m_RoutingTablePanel) {
    m_RoutingTablePanel->UpdateTimeHighlight(time);
  }

  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (weatherroute->routemapoverlay->m_bEndRouteVisible) {
      weatherroute->routemapoverlay->Render(time, m_SettingsDialog, dc, vp,
                                            true);
    }
  }

  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    (*it)->Render(time, m_SettingsDialog, dc, vp, false, m_positionOnRoute);

    if (it == currentroutemaps.begin() &&
        m_SettingsDialog.m_cbDisplayWindBarbs->GetValue())
      (*it)->RenderWindBarbs(dc, vp);
    if (it == currentroutemaps.begin() &&
        m_SettingsDialog.m_cbDisplayCurrent->GetValue())
      (*it)->RenderCurrent(dc, vp);
  }

  m_ConfigurationBatchDialog.Render(dc, vp);

#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) glPopAttrib();
#endif
}

void WeatherRouting::UpdateDisplaySettings() {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    weatherroute->routemapoverlay->m_UpdateOverlay = true;
  }

  GetParent()->Refresh();
}

void WeatherRouting::AddPosition(double lat, double lon) {
  wxTextEntryDialog pd(this, _("Enter Name"), _("New Position"));
  if (pd.ShowModal() == wxID_OK) AddPosition(lat, lon, pd.GetValue());
}

void WeatherRouting::AddPosition(double lat, double lon, wxString name) {
  for (auto& it : RouteMap::Positions) {
    if (it.GUID.IsEmpty() && it.Name == name) {
      wxMessageDialog mdlg(this, _("This name already exists, replace?\n"),
                           _("Weather Routing"), wxYES | wxNO | wxICON_WARNING);
      if (mdlg.ShowModal() == wxID_YES) {
        long index = m_panel->m_lPositions->FindItem(0, it.ID);
        assert(index >= 0);

        it.lat = lat;
        it.lon = lon;
        m_panel->m_lPositions->SetItem(
            index, POSITION_LAT,
            toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
        m_panel->m_lPositions->SetItem(
            index, POSITION_LON,
            toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
        UpdateConfigurations();
      }
      return;
    }
  }

  RouteMapPosition p(name, lat, lon);
  RouteMap::Positions.push_back(p);
  UpdateConfigurations();

  wxListItem item;
  long index = m_panel->m_lPositions->InsertItem(
      m_panel->m_lPositions->GetItemCount(), item);

  m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
  m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItem(
      index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItem(
      index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);

  m_panel->m_lPositions->SetItemData(index, p.ID);
  m_ConfigurationDialog.AddSource(name);
  m_ConfigurationBatchDialog.AddSource(name);
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::AddPosition(double lat, double lon, wxString name,
                                 wxString GUID) {
  if (GUID.IsEmpty()) return AddPosition(lat, lon, name);

  for (auto& it : RouteMap::Positions) {
    if (it.GUID.IsEmpty()) continue;

    if (it.GUID.IsSameAs(GUID)) {
      // wxMessageDialog mdlg(this, _("This name already exists,
      // replace?\n"),_("Weather Routing"), wxYES | wxNO | wxICON_WARNING);
      long index = m_panel->m_lPositions->FindItem(0, it.ID);

      it.Name = name;
      it.lat = lat;
      it.lon = lon;
      if (index >= 0) {
        m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
        m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);
        m_panel->m_lPositions->SetItem(
            index, POSITION_LAT,
            toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
        m_panel->m_lPositions->SetItem(
            index, POSITION_LON,
            toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
      }
      UpdateConfigurations();
      m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
      return;
    }
  }

  RouteMapPosition p(name, lat, lon, GUID);
  RouteMap::Positions.push_back(p);
  UpdateConfigurations();

  wxListItem item;
  long index = m_panel->m_lPositions->InsertItem(
      m_panel->m_lPositions->GetItemCount(), item);
  m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
  m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);

  m_panel->m_lPositions->SetItem(
      index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItem(
      index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItemData(index, p.ID);

  m_ConfigurationDialog.AddSource(name);
  m_ConfigurationBatchDialog.AddSource(name);
}

void WeatherRouting::AddRoute(wxString& GUID) {
  RouteMapConfiguration configuration;
  if (FirstCurrentRouteMap())
    configuration = FirstCurrentRouteMap()->GetConfiguration();
  else
    configuration = DefaultConfiguration();

  configuration.RouteGUID = GUID;
  configuration.StartTime = wxDateTime::Now();
  configuration.DeltaTime = 3600;

  if (!AddConfiguration(configuration)) return;
#if 0
    for(int i=0; i<m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
        WeatherRoute *weatherroute =
            reinterpret_cast<WeatherRoute*>(wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
        if(weatherroute->routemapoverlay->m_bEndRouteVisible)
            weatherroute->routemapoverlay->Render(time, m_SettingsDialog, dc, vp, true);
    }
#endif
  if (!IsShown()) Show(true);

  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

bool WeatherRouting::CreateMultiLegConfigurationsFromRoute(
    const wxString& routeGuid) {
  std::vector<RouteWaypointInfo> waypoints;
  wxString error;
  if (!ExtractOpenCPNRouteWaypoints(routeGuid, waypoints, error)) {
    wxLogMessage("WeatherRouting multi-leg leg creation failed: route=%s "
                 "error=%s",
                 routeGuid, error);
    wxMessageBox(error, _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  RouteMapConfiguration base;
  if (FirstCurrentRouteMap())
    base = FirstCurrentRouteMap()->GetConfiguration();
  else
    base = DefaultConfiguration();

  base.RouteGUID.Clear();
  base.DepartureTimeOptimizationEnabled = false;
  base.DepartureTimeOptimizationCandidate = false;
  base.DepartureTimeOptimizationGroupId.Clear();
  base.DepartureTimeOptimizationOffsetMinutes = 0;
  base.IsMultiLegGenerated = false;
  base.MultiLegGroupId.Clear();
  base.MultiLegParentRouteGUID.Clear();
  base.MultiLegParentRouteName.Clear();
  base.MultiLegLegIndex = 0;
  base.MultiLegLegCount = 0;

  wxString routeName = GetRouteNameForGuid(routeGuid);
  wxString groupId =
      wxString::Format(_T("multileg-%s-%s"), routeGuid,
                       wxDateTime::UNow().FormatISOCombined());
  size_t legCount = waypoints.size() - 1;
  int added = 0;

  wxLogMessage(
      "WeatherRouting multi-leg leg creation: route=%s name=%s legs=%lu",
      routeGuid, routeName, static_cast<unsigned long>(legCount));

  for (size_t i = 0; i < legCount; ++i) {
    const RouteWaypointInfo& from = waypoints[i];
    const RouteWaypointInfo& to = waypoints[i + 1];

    RouteMapConfiguration leg = base;
    leg.RouteGUID.Clear();
    leg.StartType = RouteMapConfiguration::START_FROM_WAYPOINT;
    leg.EndType = RouteMapConfiguration::END_AT_WAYPOINT;
    leg.Start = from.name;
    leg.StartGUID = from.guid;
    leg.StartLat = from.lat;
    leg.StartLon = from.lon;
    leg.End = to.name;
    leg.EndGUID = to.guid;
    leg.EndLat = to.lat;
    leg.EndLon = to.lon;
    leg.DepartureTimeOptimizationEnabled = false;
    leg.DepartureTimeOptimizationCandidate = false;
    leg.DepartureTimeOptimizationGroupId.Clear();
    leg.DepartureTimeOptimizationOffsetMinutes = 0;
    leg.IsMultiLegGenerated = true;
    leg.MultiLegGroupId = groupId;
    leg.MultiLegParentRouteGUID = routeGuid;
    leg.MultiLegParentRouteName = routeName;
    leg.MultiLegLegIndex = i + 1;
    leg.MultiLegLegCount = legCount;

    wxString legName = wxString::Format(
        "Multi-leg %s Leg %lu/%lu: %s to %s", routeName,
        static_cast<unsigned long>(i + 1), static_cast<unsigned long>(legCount),
        from.name, to.name);
    wxLogMessage(
        "WeatherRouting multi-leg leg created: route=%s leg=%lu/%lu name=%s "
        "start=%s start_guid=%s end=%s end_guid=%s",
        routeGuid, static_cast<unsigned long>(i + 1),
        static_cast<unsigned long>(legCount), legName, leg.Start,
        leg.StartGUID, leg.End, leg.EndGUID);

    if (AddConfiguration(leg)) ++added;
  }

  if (!added) {
    wxString message = _("No Weather Routing leg configurations were created.");
    wxLogMessage("WeatherRouting multi-leg leg creation failed: route=%s "
                 "error=%s",
                 routeGuid, message);
    wxMessageBox(message, _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  if (!IsShown()) Show(true);
  BeginMultiLegGroupSettingsEdit(groupId);
  m_tAutoSaveXML.Start(5000, true);
  return true;
}

std::vector<RouteMapOverlay*> WeatherRouting::GetMultiLegGroupRoutes(
    const wxString& groupId) {
  std::vector<RouteMapOverlay*> routes;
  if (groupId.IsEmpty()) return routes;

  for (auto weatherroute : m_WeatherRoutes) {
    RouteMapOverlay* routemap = weatherroute->routemapoverlay;
    RouteMapConfiguration configuration = routemap->GetConfiguration();
    if (configuration.IsMultiLegGenerated &&
        configuration.MultiLegGroupId == groupId)
      routes.push_back(routemap);
  }

  std::sort(routes.begin(), routes.end(),
            [](RouteMapOverlay* a, RouteMapOverlay* b) {
              RouteMapConfiguration ca = a->GetConfiguration();
              RouteMapConfiguration cb = b->GetConfiguration();
              return ca.MultiLegLegIndex < cb.MultiLegLegIndex;
            });
  return routes;
}

void WeatherRouting::SelectMultiLegGroup(const wxString& groupId) {
  if (!m_panel || groupId.IsEmpty()) return;

  m_bSkipUpdateCurrentItems = true;
  for (long i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); ++i) {
    m_panel->m_lWeatherRoutes->SetItemState(i, 0, wxLIST_STATE_SELECTED);

    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (!weatherroute || !weatherroute->routemapoverlay) continue;

    RouteMapConfiguration configuration =
        weatherroute->routemapoverlay->GetConfiguration();
    if (configuration.IsMultiLegGenerated &&
        configuration.MultiLegGroupId == groupId)
      m_panel->m_lWeatherRoutes->SetItemState(i, wxLIST_STATE_SELECTED,
                                              wxLIST_STATE_SELECTED);
  }
  m_bSkipUpdateCurrentItems = false;

  OnWeatherRouteSelected();
}

void WeatherRouting::PreserveMultiLegLegFields(
    RouteMapOverlay* routemapoverlay, RouteMapConfiguration& configuration)
    const {
  auto it = m_MultiLegLegSnapshots.find(routemapoverlay);
  if (it == m_MultiLegLegSnapshots.end()) return;

  const RouteMapConfiguration& original = it->second;
  configuration.RouteGUID.Clear();
  configuration.StartType = original.StartType;
  configuration.Start = original.Start;
  configuration.StartGUID = original.StartGUID;
  configuration.StartLat = original.StartLat;
  configuration.StartLon = original.StartLon;
  configuration.EndType = original.EndType;
  configuration.End = original.End;
  configuration.EndGUID = original.EndGUID;
  configuration.EndLat = original.EndLat;
  configuration.EndLon = original.EndLon;
  configuration.IsMultiLegGenerated = true;
  configuration.MultiLegGroupId = original.MultiLegGroupId;
  configuration.MultiLegParentRouteGUID = original.MultiLegParentRouteGUID;
  configuration.MultiLegParentRouteName = original.MultiLegParentRouteName;
  configuration.MultiLegLegIndex = original.MultiLegLegIndex;
  configuration.MultiLegLegCount = original.MultiLegLegCount;
  configuration.DepartureTimeOptimizationEnabled = false;
  configuration.DepartureTimeOptimizationCandidate = false;
  configuration.DepartureTimeOptimizationGroupId.Clear();
  configuration.DepartureTimeOptimizationOffsetMinutes = 0;
}

void WeatherRouting::BeginMultiLegGroupSettingsEdit(const wxString& groupId) {
  std::vector<RouteMapOverlay*> routes = GetMultiLegGroupRoutes(groupId);
  if (routes.empty()) return;

  m_ApplyingMultiLegGroupSettings = true;
  m_MultiLegSettingsGroupId = groupId;
  m_MultiLegLegSnapshots.clear();

  for (auto route : routes)
    m_MultiLegLegSnapshots[route] = route->GetConfiguration();

  SelectMultiLegGroup(groupId);
  if (!IsShown()) Show(true);

  wxLogMessage(
      "WeatherRouting multi-leg group settings edit started: group=%s "
      "legs=%lu",
      groupId, static_cast<unsigned long>(routes.size()));

  wxMessageBox(
      _("Edit shared settings for this multi-leg passage. Start and end "
        "waypoints are preserved separately for each leg. The generated legs "
        "will be reset when settings are changed."),
      _("Weather Routing"), wxOK | wxICON_INFORMATION, this);
  m_ConfigurationDialog.Show();
  m_ConfigurationDialog.Raise();
}

bool WeatherRouting::EditMultiLegGroupSettings(RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) return false;

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  if (!selected.IsMultiLegGenerated || selected.MultiLegGroupId.IsEmpty()) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  if (m_ActiveMultiLegSequence &&
      selected.MultiLegGroupId == m_ActiveMultiLegGroupId) {
    wxMessageBox(
        _("Stop the active multi-leg sequence before editing group settings."),
        _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  std::vector<RouteMapOverlay*> routes =
      GetMultiLegGroupRoutes(selected.MultiLegGroupId);
  for (auto route : routes) {
    if (RouteMapIsWaitingOrRunning(route)) {
      wxMessageBox(_("Stop the selected multi-leg routes before editing group "
                    "settings."),
                   _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return false;
    }
  }

  BeginMultiLegGroupSettingsEdit(selected.MultiLegGroupId);
  return true;
}

void WeatherRouting::ShowRoutingStatus(RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) {
    wxMessageBox(_("Select a weather route row first."), _("Weather Routing"),
                 wxOK | wxICON_WARNING, this);
    return;
  }

  auto appendRouteStatus = [&](wxString& text, RouteMapOverlay* route) {
    if (!route) return;
    RouteMapConfiguration configuration = route->GetConfiguration();

    WeatherRoute display;
    display.routemapoverlay = route;
    display.Update(this);

    wxString state = display.State;
    if (route->Finished() && !route->ReachedDestination()) {
      wxString reason = SafeMultiLegFailureReason(route);
      if (!reason.IsEmpty() && state.Find(reason) == wxNOT_FOUND) {
        if (!state.IsEmpty()) state += _T(": ");
        state += reason;
      }
    }

    if (configuration.IsMultiLegGenerated) {
      text += wxString::Format(_("Leg %d/%d: %s to %s\n"),
                               configuration.MultiLegLegIndex,
                               configuration.MultiLegLegCount,
                               configuration.Start, configuration.End);
    } else {
      text += wxString::Format(_("%s to %s\n"), configuration.Start,
                               configuration.End);
    }
    text += wxString::Format(_("State: %s\n"), state);
    text += wxString::Format(_("Start: %s\n"), display.StartTime);
    text += wxString::Format(_("End: %s\n"), display.EndTime);
    text += wxString::Format(_("Time: %s\n"), display.Time);
    text += wxString::Format(_("Distance: %s\n"), display.Distance);
  };

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  wxString message;
  if (selected.IsMultiLegGenerated && !selected.MultiLegGroupId.IsEmpty()) {
    std::vector<RouteMapOverlay*> routes =
        GetMultiLegGroupRoutes(selected.MultiLegGroupId);
    message = wxString::Format(_("Multi-leg routing status: %s\n\n"),
                               selected.MultiLegParentRouteName);
    for (size_t i = 0; i < routes.size(); ++i) {
      appendRouteStatus(message, routes[i]);
      if (i + 1 < routes.size()) message += _T("\n");
    }
  } else {
    message = _("Routing status\n\n");
    appendRouteStatus(message, selectedRoute);
  }

  wxMessageBox(message, _("Weather Routing Status"), wxOK | wxICON_INFORMATION,
               this);
}

bool WeatherRouting::RouteMapIsWaitingOrRunning(
    RouteMapOverlay* routemapoverlay) const {
  if (!routemapoverlay) return false;
  if (routemapoverlay->Running()) return true;
  if (std::find(m_RunningRouteMaps.begin(), m_RunningRouteMaps.end(),
                routemapoverlay) != m_RunningRouteMaps.end())
    return true;
  if (std::find(m_WaitingRouteMaps.begin(), m_WaitingRouteMaps.end(),
                routemapoverlay) != m_WaitingRouteMaps.end())
    return true;
  return false;
}

bool WeatherRouting::RouteMapIsManaged(RouteMapOverlay* routemapoverlay) const {
  if (!routemapoverlay) return false;
  for (auto weatherroute : m_WeatherRoutes)
    if (weatherroute && weatherroute->routemapoverlay == routemapoverlay)
      return true;
  return false;
}

wxString WeatherRouting::SafeMultiLegFailureReason(
    RouteMapOverlay* routemapoverlay) const {
  if (!routemapoverlay) return _("route is unavailable");
  if (!RouteMapIsManaged(routemapoverlay)) return _("route is no longer active");
  if (routemapoverlay->Running()) return _("leg is still running");
  if (!routemapoverlay->Finished()) return _("leg was stopped");

  wxString reason;
  wxString explicitReason = routemapoverlay->GetFailureReason();
  if (!explicitReason.IsEmpty()) return explicitReason;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  if (configuration.grib_is_data_deficient) reason += _("data deficient");

  WeatherForecastStatus forecastStatus =
      routemapoverlay->GetWeatherForecastStatus();
  if (forecastStatus != WEATHER_FORECAST_SUCCESS) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += RouteMap::GetWeatherForecastStatusMessage(forecastStatus);
  }

  wxString weatherStatus = routemapoverlay->GetWeatherForecastError();
  if (!weatherStatus.IsEmpty()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += weatherStatus;
  }

  wxString gribStatus = routemapoverlay->GetGribError();
  if (!gribStatus.IsEmpty()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += gribStatus;
  }

  PolarSpeedStatus polarStatus = routemapoverlay->GetPolarStatus();
  if (polarStatus != POLAR_SPEED_SUCCESS) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("Polar: ");
    reason += Polar::GetPolarStatusMessage(polarStatus);
  }

  if (routemapoverlay->LandCrossing()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("land crossing");
  }

  if (routemapoverlay->BoundaryCrossing()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("boundary crossing");
  }

  if (routemapoverlay->Finished() && !routemapoverlay->ReachedDestination()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("leg did not reach destination");
  }

  if (routemapoverlay->Finished() && routemapoverlay->ReachedDestination() &&
      !routemapoverlay->EndTime().IsValid()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("leg completed without a valid ETA");
  }

  if (reason.IsEmpty()) reason = _("leg failed");
  return reason;
}

void WeatherRouting::CancelMultiLegSequence() {
  if (!m_ActiveMultiLegSequence) return;
  wxLogMessage("WeatherRouting multi-leg sequence cancelled: group=%s",
               m_ActiveMultiLegGroupId);
  m_ActiveMultiLegSequence = false;
  m_ActiveMultiLegGroupId.Clear();
  m_ActiveMultiLegCurrentLegIndex = 0;
}

bool WeatherRouting::StartMultiLegSequenceLeg(RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  wxLogMessage(
      "WeatherRouting multi-leg sequence starting leg %d/%d: group=%s "
      "start=%s end=%s departure=%s",
      configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
      configuration.MultiLegGroupId, configuration.Start, configuration.End,
      configuration.StartTime.FormatISOCombined());

  if (RouteMapIsWaitingOrRunning(routemapoverlay)) {
    Stop(routemapoverlay);
    m_RunningRouteMaps.remove(routemapoverlay);
    m_WaitingRouteMaps.remove(routemapoverlay);
  }
  routemapoverlay->Reset();
  UpdateRouteMap(routemapoverlay);
  Start(routemapoverlay);

  if (!RouteMapIsWaitingOrRunning(routemapoverlay)) {
    UpdateRouteMap(routemapoverlay);
    wxLogMessage(
        "WeatherRouting multi-leg sequence could not start leg %d/%d: "
        "group=%s",
        configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
        configuration.MultiLegGroupId);
    return false;
  }

  m_panel->m_gProgress->SetRange(m_RoutesToRun);
  return true;
}

bool WeatherRouting::ComputeMultiLegSequence(RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) return false;

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  if (!selected.IsMultiLegGenerated || selected.MultiLegGroupId.IsEmpty()) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  CancelMultiLegSequence();

  std::vector<RouteMapOverlay*> routes =
      GetMultiLegGroupRoutes(selected.MultiLegGroupId);
  if ((int)routes.size() != selected.MultiLegLegCount || routes.empty()) {
    wxString message =
        _("The selected multi-leg group is incomplete or unavailable.");
    wxLogMessage("WeatherRouting multi-leg sequence failed: group=%s error=%s",
                 selected.MultiLegGroupId, message);
    wxMessageBox(message, _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  for (auto route : routes) {
    RouteMapConfiguration configuration = route->GetConfiguration();
    if (RouteMapIsWaitingOrRunning(route)) {
      Stop(route);
      m_RunningRouteMaps.remove(route);
      m_WaitingRouteMaps.remove(route);
    }
    route->Reset();
    route->SetConfiguration(configuration);
    UpdateRouteMap(route);
  }

  RouteMapConfiguration first = routes.front()->GetConfiguration();
  m_ActiveMultiLegGroupId = first.MultiLegGroupId;
  m_ActiveMultiLegCurrentLegIndex = 1;
  m_ActiveMultiLegSequence = true;

  wxLogMessage(
      "WeatherRouting multi-leg sequence starting: group=%s route=%s legs=%d",
      first.MultiLegGroupId, first.MultiLegParentRouteName,
      first.MultiLegLegCount);

  if (!StartMultiLegSequenceLeg(routes.front())) {
    CancelMultiLegSequence();
    return false;
  }

  UpdateComputeState();
  return true;
}

void WeatherRouting::AdvanceMultiLegSequence(RouteMapOverlay* completedRoute) {
  if (!m_ActiveMultiLegSequence || !completedRoute) return;
  if (!RouteMapIsManaged(completedRoute)) {
    wxLogMessage(
        "WeatherRouting multi-leg sequence stopped: group=%s route pointer is "
        "no longer active",
        m_ActiveMultiLegGroupId);
    CancelMultiLegSequence();
    return;
  }

  RouteMapConfiguration completed = completedRoute->GetConfiguration();
  if (!completed.IsMultiLegGenerated ||
      completed.MultiLegGroupId != m_ActiveMultiLegGroupId ||
      completed.MultiLegLegIndex != m_ActiveMultiLegCurrentLegIndex)
    return;

  if (!completedRoute->Finished()) return;

  bool reachedDestination = completedRoute->ReachedDestination();
  wxDateTime eta = completedRoute->EndTime();
  wxLogMessage(
      "WeatherRouting multi-leg sequence leg state: group=%s leg=%d/%d "
      "route=%p finished=%d reached=%d eta_valid=%d",
      completed.MultiLegGroupId, completed.MultiLegLegIndex,
      completed.MultiLegLegCount, completedRoute, completedRoute->Finished(),
      reachedDestination, eta.IsValid());

  if (!completedRoute->ReachedDestination() ||
      !completedRoute->EndTime().IsValid()) {
    wxString reason = SafeMultiLegFailureReason(completedRoute);
    wxLogMessage(
        "WeatherRouting multi-leg sequence stopped at leg %d/%d: group=%s "
        "%s -> %s start_time=%s reached=%d eta_valid=%d reason=%s",
        completed.MultiLegLegIndex, completed.MultiLegLegCount,
        completed.MultiLegGroupId, completed.Start, completed.End,
        completed.StartTime.FormatISOCombined(),
        completedRoute->ReachedDestination(), completedRoute->EndTime().IsValid(),
        reason);
    CancelMultiLegSequence();
    return;
  }

  wxLogMessage(
      "WeatherRouting multi-leg sequence completed leg %d/%d: group=%s "
      "eta=%s",
      completed.MultiLegLegIndex, completed.MultiLegLegCount,
      completed.MultiLegGroupId, eta.FormatISOCombined());

  if (completed.MultiLegLegIndex >= completed.MultiLegLegCount) {
    wxLogMessage(
        "WeatherRouting multi-leg sequence complete: group=%s final_eta=%s",
        completed.MultiLegGroupId, eta.FormatISOCombined());
    CancelMultiLegSequence();
    return;
  }

  std::vector<RouteMapOverlay*> routes =
      GetMultiLegGroupRoutes(completed.MultiLegGroupId);
  RouteMapOverlay* nextRoute = NULL;
  for (auto route : routes) {
    RouteMapConfiguration configuration = route->GetConfiguration();
    if (configuration.MultiLegLegIndex == completed.MultiLegLegIndex + 1) {
      nextRoute = route;
      break;
    }
  }

  if (!nextRoute) {
    wxLogMessage(
        "WeatherRouting multi-leg sequence stopped: missing leg %d in "
        "group=%s",
        completed.MultiLegLegIndex + 1, completed.MultiLegGroupId);
    CancelMultiLegSequence();
    return;
  }

  RouteMapConfiguration next = nextRoute->GetConfiguration();
  next.StartTime = eta;
  next.UseCurrentTime = false;
  nextRoute->SetConfiguration(next);
  UpdateRouteMap(nextRoute);
  m_ActiveMultiLegCurrentLegIndex = next.MultiLegLegIndex;

  if (!StartMultiLegSequenceLeg(nextRoute)) CancelMultiLegSequence();
}

void WeatherRouting::CancelMultiLegDepartureOptimization(
    bool cleanupCandidates) {
  if (m_ActiveMultiLegDepartureOptimization)
    wxLogMessage("WeatherRouting multi-leg departure optimisation cancelled: "
                 "id=%s",
                 m_ActiveMultiLegOptimizationId);

  for (auto& candidate : m_MultiLegOptimizationCandidates) {
    for (auto route : candidate.routes) {
      if (route && RouteMapIsWaitingOrRunning(route)) Stop(route);
    }
  }

  if (cleanupCandidates) {
    std::list<RouteMapOverlay*> routesToDelete;
    for (auto& candidate : m_MultiLegOptimizationCandidates)
      for (auto route : candidate.routes)
        if (route && RouteMapIsManaged(route)) routesToDelete.push_back(route);
    DeleteRouteMaps(routesToDelete);
    m_MultiLegOptimizationCandidates.clear();
  }

  m_ActiveMultiLegDepartureOptimization = false;
  m_ActiveMultiLegOptimizationId.Clear();
  m_MultiLegOptimizationBaseGroupId.Clear();
  m_ActiveMultiLegOptimizationCandidateIndex = -1;
  m_ActiveMultiLegOptimizationLegIndex = 0;
  m_AppliedMultiLegOptimizationCandidateIndex = -1;
}

void WeatherRouting::DeleteMultiLegOptimizationCandidateRows() {
  std::list<RouteMapOverlay*> routesToDelete;
  for (auto& candidate : m_MultiLegOptimizationCandidates) {
    for (auto route : candidate.routes)
      if (route && RouteMapIsManaged(route)) routesToDelete.push_back(route);
    candidate.routes.clear();
  }
  DeleteRouteMaps(routesToDelete);
}

bool WeatherRouting::HasCompleteMultiLegOptimizationCandidate() const {
  for (const auto& candidate : m_MultiLegOptimizationCandidates)
    if (candidate.complete) return true;
  return false;
}

bool WeatherRouting::ApplyMultiLegOptimizationCandidate(int candidateIndex) {
  if (m_ActiveMultiLegDepartureOptimization) {
    wxMessageBox(_("Wait for the optimisation to finish before applying a "
                  "candidate."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }
  if (m_AppliedMultiLegOptimizationCandidateIndex >= 0) {
    if (m_AppliedMultiLegOptimizationCandidateIndex == candidateIndex)
      return true;
    wxMessageBox(_("A multi-leg departure candidate has already been applied. "
                  "Run the optimisation again to choose a different result."),
                 _("Weather Routing"), wxOK | wxICON_INFORMATION, this);
    return false;
  }
  if (candidateIndex < 0 ||
      candidateIndex >= (int)m_MultiLegOptimizationCandidates.size())
    return false;

  MultiLegOptimizationCandidate& candidate =
      m_MultiLegOptimizationCandidates[candidateIndex];
  if (!candidate.complete) {
    wxMessageBox(_("Only complete multi-leg candidates can be applied."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  std::vector<RouteMapOverlay*> baseRoutes =
      GetMultiLegGroupRoutes(m_MultiLegOptimizationBaseGroupId);
  if (baseRoutes.size() != candidate.routes.size() || baseRoutes.empty()) {
    wxMessageBox(_("The original multi-leg group is no longer available."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  for (size_t i = 0; i < baseRoutes.size(); ++i) {
    RouteMapOverlay* baseRoute = baseRoutes[i];
    RouteMapOverlay* candidateRoute = candidate.routes[i];
    if (!baseRoute || !candidateRoute || !RouteMapIsManaged(baseRoute) ||
        !RouteMapIsManaged(candidateRoute)) {
      wxLogMessage(
          "WeatherRouting multi-leg departure optimisation apply failed: "
          "candidate=%d leg=%zu base=%p candidate_route=%p",
          candidateIndex, i + 1, baseRoute, candidateRoute);
      wxMessageBox(_("The selected multi-leg candidate is no longer available."),
                   _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return false;
    }
  }

  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation applying: index=%d "
      "offset=%d base_rows=%zu candidate_rows=%zu",
      candidateIndex, candidate.offsetMinutes, baseRoutes.size(),
      candidate.routes.size());

  auto findWeatherRoute = [&](RouteMapOverlay* route) -> WeatherRoute* {
    for (auto weatherroute : m_WeatherRoutes)
      if (weatherroute->routemapoverlay == route) return weatherroute;
    return NULL;
  };

  std::vector<RouteMapConfiguration> originalCandidateConfigs;
  std::vector<bool> originalCandidateFiltered;
  originalCandidateConfigs.reserve(candidate.routes.size());
  originalCandidateFiltered.reserve(candidate.routes.size());
  for (auto route : candidate.routes) {
    originalCandidateConfigs.push_back(route->GetConfiguration());
    WeatherRoute* weatherroute = findWeatherRoute(route);
    originalCandidateFiltered.push_back(weatherroute ? weatherroute->Filtered
                                                     : false);
  }

  std::vector<RouteMapConfiguration> originalBaseConfigs;
  std::vector<bool> originalBaseFiltered;
  originalBaseConfigs.reserve(baseRoutes.size());
  originalBaseFiltered.reserve(baseRoutes.size());
  for (auto route : baseRoutes) {
    originalBaseConfigs.push_back(route->GetConfiguration());
    WeatherRoute* weatherroute = findWeatherRoute(route);
    originalBaseFiltered.push_back(weatherroute ? weatherroute->Filtered
                                                : false);
  }

  std::vector<std::pair<RouteMapOverlay*, bool> > originalOtherCandidateFilter;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    if ((int)i == candidateIndex) continue;
    for (auto route : m_MultiLegOptimizationCandidates[i].routes) {
      WeatherRoute* weatherroute = findWeatherRoute(route);
      originalOtherCandidateFilter.push_back(std::make_pair(
          route, weatherroute ? weatherroute->Filtered : false));
    }
  }

  auto rollbackPromotedCandidate = [&]() {
    for (size_t i = 0; i < candidate.routes.size(); ++i) {
      if (candidate.routes[i])
        candidate.routes[i]->SetConfigurationPreserveResult(
            originalCandidateConfigs[i]);
      WeatherRoute* weatherroute = findWeatherRoute(candidate.routes[i]);
      if (weatherroute) weatherroute->Filtered = originalCandidateFiltered[i];
    }
    for (size_t i = 0; i < baseRoutes.size(); ++i) {
      if (baseRoutes[i])
        baseRoutes[i]->SetConfigurationPreserveResult(originalBaseConfigs[i]);
      WeatherRoute* weatherroute = findWeatherRoute(baseRoutes[i]);
      if (weatherroute) weatherroute->Filtered = originalBaseFiltered[i];
    }
    for (auto entry : originalOtherCandidateFilter) {
      WeatherRoute* weatherroute = findWeatherRoute(entry.first);
      if (weatherroute) weatherroute->Filtered = entry.second;
    }
  };

  auto visibleAppliedRows = [&](const wxString& groupId) {
    int count = 0;
    if (!m_panel || !m_panel->m_lWeatherRoutes) return count;
    for (long i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); ++i) {
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
      if (!weatherroute || !weatherroute->routemapoverlay) continue;
      RouteMapConfiguration configuration =
          weatherroute->routemapoverlay->GetConfiguration();
      if (configuration.IsMultiLegGenerated &&
          !configuration.DepartureTimeOptimizationCandidate &&
          configuration.MultiLegGroupId == groupId)
        count++;
    }
    return count;
  };

  for (size_t i = 0; i < baseRoutes.size(); ++i) {
    RouteMapOverlay* baseRoute = baseRoutes[i];
    RouteMapOverlay* candidateRoute = candidate.routes[i];

    RouteMapConfiguration baseConfig = baseRoute->GetConfiguration();
    RouteMapConfiguration appliedConfig = candidateRoute->GetConfiguration();
    appliedConfig.RouteGUID.Clear();
    appliedConfig.StartType = baseConfig.StartType;
    appliedConfig.Start = baseConfig.Start;
    appliedConfig.StartGUID = baseConfig.StartGUID;
    appliedConfig.StartLat = baseConfig.StartLat;
    appliedConfig.StartLon = baseConfig.StartLon;
    appliedConfig.EndType = baseConfig.EndType;
    appliedConfig.End = baseConfig.End;
    appliedConfig.EndGUID = baseConfig.EndGUID;
    appliedConfig.EndLat = baseConfig.EndLat;
    appliedConfig.EndLon = baseConfig.EndLon;
    appliedConfig.DepartureTimeOptimizationEnabled =
        baseConfig.DepartureTimeOptimizationEnabled;
    appliedConfig.DepartureTimeOptimizationCandidate = false;
    appliedConfig.DepartureTimeOptimizationGroupId.Clear();
    appliedConfig.DepartureTimeOptimizationOffsetMinutes = 0;
    appliedConfig.IsMultiLegGenerated = true;
    appliedConfig.MultiLegGroupId = baseConfig.MultiLegGroupId;
    appliedConfig.MultiLegParentRouteGUID = baseConfig.MultiLegParentRouteGUID;
    appliedConfig.MultiLegParentRouteName = baseConfig.MultiLegParentRouteName;
    appliedConfig.MultiLegLegIndex = baseConfig.MultiLegLegIndex;
    appliedConfig.MultiLegLegCount = baseConfig.MultiLegLegCount;
    candidateRoute->SetConfigurationPreserveResult(appliedConfig);

    WeatherRoute* candidateWeatherRoute = findWeatherRoute(candidateRoute);
    if (candidateWeatherRoute) candidateWeatherRoute->Filtered = false;
  }

  int retiredBaseRows = 0;
  for (auto route : baseRoutes) {
    if (!route || !RouteMapIsManaged(route)) continue;
    RouteMapConfiguration retiredConfig = route->GetConfiguration();
    retiredConfig.DepartureTimeOptimizationCandidate = true;
    retiredConfig.DepartureTimeOptimizationGroupId =
        m_ActiveMultiLegOptimizationId;
    retiredConfig.MultiLegGroupId =
        retiredConfig.MultiLegGroupId + _T("-replaced");
    route->SetConfigurationPreserveResult(retiredConfig);
    WeatherRoute* weatherroute = findWeatherRoute(route);
    if (weatherroute) weatherroute->Filtered = true;
    retiredBaseRows++;
  }

  int removedTemporaryRows = 0;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    MultiLegOptimizationCandidate& cleanupCandidate =
        m_MultiLegOptimizationCandidates[i];
    if ((int)i == candidateIndex) {
      cleanupCandidate.routes.clear();
      continue;
    }

    for (auto route : cleanupCandidate.routes) {
      if (route && RouteMapIsManaged(route)) {
        WeatherRoute* weatherroute = findWeatherRoute(route);
        if (weatherroute) weatherroute->Filtered = true;
        removedTemporaryRows++;
      }
    }
  }

  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation applied: index=%d "
      "offset=%d promoted_rows=%zu removed_temporary_rows=%d "
      "retired_base_rows=%d",
      candidateIndex, candidate.offsetMinutes, baseRoutes.size(),
      removedTemporaryRows, retiredBaseRows);

  RebuildList();
  int visibleRows = visibleAppliedRows(m_MultiLegOptimizationBaseGroupId);
  std::vector<RouteMapOverlay*> appliedRoutes =
      GetMultiLegGroupRoutes(m_MultiLegOptimizationBaseGroupId);
  if (appliedRoutes.size() != baseRoutes.size() ||
      visibleRows != (int)baseRoutes.size()) {
    rollbackPromotedCandidate();
    RebuildList();
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation post-apply "
        "verification failed: index=%d applied_group_rows=%zu visible_rows=%d "
        "expected=%zu",
        candidateIndex, appliedRoutes.size(), visibleRows, baseRoutes.size());
    wxMessageBox(
        _("The selected multi-leg candidate was applied, but the route list did "
          "not refresh as expected. The candidate rows were not deleted."),
        _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i)
    m_MultiLegOptimizationCandidates[i].applied = false;
  candidate.applied = true;
  m_AppliedMultiLegOptimizationCandidateIndex = candidateIndex;

  SelectMultiLegGroup(m_MultiLegOptimizationBaseGroupId);
  UpdateDialogs();
  GetParent()->Refresh();
  m_tAutoSaveXML.Start(5000, true);
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation apply complete: "
      "weather_route_count=%zu group=%s",
      m_WeatherRoutes.size(), m_MultiLegOptimizationBaseGroupId);
  return true;
}

bool WeatherRouting::ApplyBestMultiLegOptimizationCandidate() {
  int bestIndex = -1;
  long bestSeconds = std::numeric_limits<long>::max();
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    const MultiLegOptimizationCandidate& candidate =
        m_MultiLegOptimizationCandidates[i];
    if (!candidate.complete) continue;
    if (candidate.best ||
        (candidate.totalElapsedSeconds >= 0 &&
         candidate.totalElapsedSeconds < bestSeconds)) {
      bestIndex = i;
      bestSeconds = candidate.totalElapsedSeconds;
      if (candidate.best) break;
    }
  }
  return ApplyMultiLegOptimizationCandidate(bestIndex);
}

bool WeatherRouting::StartMultiLegOptimizationLeg(
    RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation starting candidate=%d "
      "leg=%d/%d departure=%s start=%s end=%s",
      configuration.DepartureTimeOptimizationOffsetMinutes,
      configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
      configuration.StartTime.FormatISOCombined(), configuration.Start,
      configuration.End);

  if (RouteMapIsWaitingOrRunning(routemapoverlay)) {
    Stop(routemapoverlay);
    m_RunningRouteMaps.remove(routemapoverlay);
    m_WaitingRouteMaps.remove(routemapoverlay);
  }
  routemapoverlay->Reset();
  UpdateRouteMap(routemapoverlay);
  Start(routemapoverlay);

  if (!RouteMapIsWaitingOrRunning(routemapoverlay)) {
    UpdateRouteMap(routemapoverlay);
    return false;
  }

  m_panel->m_gProgress->SetRange(m_RoutesToRun);
  return true;
}

bool WeatherRouting::StartNextMultiLegOptimizationCandidate() {
  if (!m_ActiveMultiLegDepartureOptimization) return false;

  m_ActiveMultiLegOptimizationCandidateIndex++;
  m_ActiveMultiLegOptimizationLegIndex = 1;

  while (m_ActiveMultiLegOptimizationCandidateIndex <
         (int)m_MultiLegOptimizationCandidates.size()) {
    MultiLegOptimizationCandidate& candidate =
        m_MultiLegOptimizationCandidates
            [m_ActiveMultiLegOptimizationCandidateIndex];
    if (candidate.failed && (int)candidate.routes.size() != candidate.totalLegs) {
      m_ActiveMultiLegOptimizationCandidateIndex++;
      continue;
    }
    if (candidate.routes.empty()) {
      candidate.failed = true;
      candidate.state = _("Failed");
      candidate.reason = _("No route legs were created for this candidate.");
      m_ActiveMultiLegOptimizationCandidateIndex++;
      continue;
    }

    candidate.running = true;
    candidate.state = _("Computing...");
    candidate.reason.Clear();
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation candidate started: "
        "index=%d offset=%d departure=%s",
        m_ActiveMultiLegOptimizationCandidateIndex, candidate.offsetMinutes,
        candidate.departureTime.FormatISOCombined());

    if (!StartMultiLegOptimizationLeg(candidate.routes.front())) {
      candidate.running = false;
      candidate.failed = true;
      candidate.state = _("Failed");
      candidate.reason = _("Could not start first leg.");
      m_ActiveMultiLegOptimizationCandidateIndex++;
      continue;
    }

    UpdateComputeState();
    return true;
  }

  long bestSeconds = std::numeric_limits<long>::max();
  int bestIndex = -1;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    MultiLegOptimizationCandidate& candidate =
        m_MultiLegOptimizationCandidates[i];
    candidate.best = false;
    if (!candidate.complete) continue;
    if (candidate.totalElapsedSeconds >= 0 &&
        candidate.totalElapsedSeconds < bestSeconds) {
      bestSeconds = candidate.totalElapsedSeconds;
      bestIndex = i;
    }
  }
  if (bestIndex >= 0) m_MultiLegOptimizationCandidates[bestIndex].best = true;

  m_ActiveMultiLegDepartureOptimization = false;
  long completed = 0;
  long failed = 0;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    if (m_MultiLegOptimizationCandidates[i].complete)
      ++completed;
    else if (m_MultiLegOptimizationCandidates[i].failed)
      ++failed;
  }
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation complete: id=%s "
      "best_index=%d candidates_completed=%ld candidates_failed=%ld "
      "candidate_count=%lu",
      m_ActiveMultiLegOptimizationId, bestIndex, completed, failed,
      static_cast<unsigned long>(m_MultiLegOptimizationCandidates.size()));
  ConstraintChecker::LogSegmentSafetyDiagnostics(
      wxString::Format(_("multi-leg departure optimisation %s"),
                       m_ActiveMultiLegOptimizationId));
  return false;
}

void WeatherRouting::AdvanceMultiLegDepartureOptimization(
    RouteMapOverlay* completedRoute) {
  if (!m_ActiveMultiLegDepartureOptimization || !completedRoute) return;
  if (m_ActiveMultiLegOptimizationCandidateIndex < 0 ||
      m_ActiveMultiLegOptimizationCandidateIndex >=
          (int)m_MultiLegOptimizationCandidates.size())
    return;

  MultiLegOptimizationCandidate& candidate =
      m_MultiLegOptimizationCandidates
          [m_ActiveMultiLegOptimizationCandidateIndex];

  auto routeIt =
      std::find(candidate.routes.begin(), candidate.routes.end(), completedRoute);
  if (routeIt == candidate.routes.end()) return;

  int legIndex = (int)(routeIt - candidate.routes.begin()) + 1;
  if (legIndex != m_ActiveMultiLegOptimizationLegIndex) return;
  if (!completedRoute->Finished()) return;

  RouteMapConfiguration completed = completedRoute->GetConfiguration();
  wxDateTime eta = completedRoute->EndTime();
  bool completeLeg =
      completedRoute->ReachedDestination() && eta.IsValid();
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation leg state: "
      "candidate=%d leg=%d/%d finished=%d reached=%d eta_valid=%d",
      candidate.offsetMinutes, legIndex, candidate.totalLegs,
      completedRoute->Finished(), completedRoute->ReachedDestination(),
      eta.IsValid());

  if (!completeLeg) {
    candidate.running = false;
    candidate.failed = true;
    candidate.complete = false;
    candidate.completedLegs = legIndex > 0 ? legIndex - 1 : 0;
    candidate.failedLegIndex = legIndex;
    candidate.failedLegName =
        wxString::Format(_T("%s to %s"), completed.Start, completed.End);
    candidate.state = _("Failed");
    candidate.reason = SafeMultiLegFailureReason(completedRoute);
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation candidate failed: "
        "offset=%d leg=%d/%d reason=%s",
        candidate.offsetMinutes, legIndex, candidate.totalLegs,
        candidate.reason);
    StartNextMultiLegOptimizationCandidate();
    return;
  }

  candidate.completedLegs = legIndex;
  double legDistance = completedRoute->RouteInfo(RouteMapOverlay::DISTANCE);
  if (std::isfinite(legDistance)) candidate.totalDistance += legDistance;

  if (legIndex >= candidate.totalLegs) {
    candidate.running = false;
    candidate.complete = true;
    candidate.failed = false;
    candidate.finalEta = eta;
    wxTimeSpan elapsed = eta - candidate.departureTime;
    candidate.totalElapsedSeconds = elapsed.GetSeconds().ToLong();
    candidate.state = _("Complete");
    candidate.reason.Clear();
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation candidate complete: "
        "offset=%d final_eta=%s elapsed=%ld",
        candidate.offsetMinutes, eta.FormatISOCombined(),
        candidate.totalElapsedSeconds);
    StartNextMultiLegOptimizationCandidate();
    return;
  }

  RouteMapOverlay* nextRoute = candidate.routes[legIndex];
  RouteMapConfiguration next = nextRoute->GetConfiguration();
  next.StartTime = eta;
  next.UseCurrentTime = false;
  nextRoute->SetConfiguration(next);
  UpdateRouteMap(nextRoute);
  m_ActiveMultiLegOptimizationLegIndex = legIndex + 1;

  if (!StartMultiLegOptimizationLeg(nextRoute)) {
    candidate.running = false;
    candidate.failed = true;
    candidate.failedLegIndex = legIndex + 1;
    candidate.failedLegName =
        wxString::Format(_T("%s to %s"), next.Start, next.End);
    candidate.state = _("Failed");
    candidate.reason = _("Could not start next leg.");
    StartNextMultiLegOptimizationCandidate();
  }
}

bool WeatherRouting::ComputeMultiLegDepartureOptimization(
    RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) return false;

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  if (!selected.IsMultiLegGenerated || selected.MultiLegGroupId.IsEmpty()) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  std::vector<RouteMapOverlay*> baseRoutes =
      GetMultiLegGroupRoutes(selected.MultiLegGroupId);
  if (baseRoutes.empty()) return false;

  RouteMapConfiguration first = baseRoutes.front()->GetConfiguration();
  int rangeMinutes = first.DepartureTimeOptimizationRangeMinutes;
  int stepMinutes = first.DepartureTimeOptimizationStepMinutes;
  if (rangeMinutes < 0) {
    wxMessageBox(_("Departure optimisation range must not be negative."),
                 _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return false;
  }
  if (stepMinutes <= 0) {
    wxMessageBox(_("Departure optimisation step must be greater than zero."),
                 _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return false;
  }

  std::vector<int> offsets;
  for (int offset = -rangeMinutes; offset <= rangeMinutes;
       offset += stepMinutes)
    offsets.push_back(offset);
  offsets.push_back(0);
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

  if (offsets.size() > MAX_DEPARTURE_OPTIMIZATION_CANDIDATES) {
    wxMessageBox(
        wxString::Format(
            _("Departure optimisation would create %d candidate chains. "
              "Increase the step or reduce the range. The current limit is "
              "%d."),
            (int)offsets.size(), MAX_DEPARTURE_OPTIMIZATION_CANDIDATES),
        _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return false;
  }

  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);

  m_ActiveMultiLegOptimizationId =
      wxString::Format(_T("multileg-opt-%s"),
                       wxDateTime::UNow().FormatISOCombined());
  m_MultiLegOptimizationBaseGroupId = selected.MultiLegGroupId;
  m_MultiLegOptimizationCandidates.clear();
  m_AppliedMultiLegOptimizationCandidateIndex = -1;
  wxDateTime nominalStartTime = first.StartTime;

  PrewarmExperimentalChartSafetyForMultiLegEnvelope(
      baseRoutes, _("multi-leg departure optimisation"));

  for (size_t i = 0; i < baseRoutes.size(); ++i) {
    if (!baseRoutes[i]) continue;
    PrewarmExperimentalChartSafetyForConfiguration(
        baseRoutes[i]->GetConfiguration(),
        wxString::Format(_("multi-leg optimisation leg %lu/%lu"),
                         static_cast<unsigned long>(i + 1),
                         static_cast<unsigned long>(baseRoutes.size())));
  }

  for (auto offset : offsets) {
    MultiLegOptimizationCandidate candidate;
    candidate.offsetMinutes = offset;
    candidate.departureTime = nominalStartTime + wxTimeSpan::Minutes(offset);
    candidate.totalElapsedSeconds = -1;
    candidate.totalDistance = 0.0;
    candidate.completedLegs = 0;
    candidate.totalLegs = (int)baseRoutes.size();
    candidate.failedLegIndex = 0;
    candidate.complete = false;
    candidate.failed = false;
    candidate.running = false;
    candidate.best = false;
    candidate.applied = false;
    candidate.state = _("Waiting...");

    wxString candidateGroupId =
        wxString::Format(_T("%s-%+d"), m_ActiveMultiLegOptimizationId, offset);
    for (size_t i = 0; i < baseRoutes.size(); ++i) {
      RouteMapConfiguration leg = baseRoutes[i]->GetConfiguration();
      leg.RouteGUID.Clear();
      leg.DepartureTimeOptimizationEnabled = false;
      leg.DepartureTimeOptimizationCandidate = true;
      leg.DepartureTimeOptimizationNominalStartTime = nominalStartTime;
      leg.DepartureTimeOptimizationOffsetMinutes = offset;
      leg.DepartureTimeOptimizationGroupId = m_ActiveMultiLegOptimizationId;
      leg.MultiLegGroupId = candidateGroupId;
      leg.MultiLegLegIndex = i + 1;
      leg.MultiLegLegCount = baseRoutes.size();
      leg.MultiLegParentRouteName =
          wxString::Format(_("%s departure %+d min"),
                           first.MultiLegParentRouteName, offset);
      leg.StartTime = candidate.departureTime;
      leg.UseCurrentTime = false;

      if (!AddConfiguration(leg)) continue;
      RouteMapOverlay* route = m_WeatherRoutes.back()->routemapoverlay;
      route->LoadBoat();
      candidate.routes.push_back(route);
    }

    if ((int)candidate.routes.size() != candidate.totalLegs) {
      candidate.failed = true;
      candidate.state = _("Failed");
      candidate.reason = _("Could not create all candidate route legs.");
    }
    m_MultiLegOptimizationCandidates.push_back(candidate);
  }

  if (m_MultiLegOptimizationCandidates.empty()) return false;

  m_ActiveMultiLegDepartureOptimization = true;
  m_ActiveMultiLegOptimizationCandidateIndex = -1;
  m_ActiveMultiLegOptimizationLegIndex = 0;

  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation starting: id=%s "
      "candidates=%lu legs=%lu",
      m_ActiveMultiLegOptimizationId,
      static_cast<unsigned long>(m_MultiLegOptimizationCandidates.size()),
      static_cast<unsigned long>(baseRoutes.size()));

  StartNextMultiLegOptimizationCandidate();
  UpdateComputeState();
  ShowMultiLegDepartureOptimizationResults();
  return true;
}

void WeatherRouting::CursorRouteChanged() {
  if (m_PlotDialog.IsShown() && m_PlotDialog.m_rbCursorRoute->GetValue())
    m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());
}

void WeatherRouting::UpdateColumns() {
  m_panel->m_lWeatherRoutes->DeleteAllColumns();

  for (int i = 0; i < NUM_COLS; i++) {
    if (m_SettingsDialog.m_cblFields->IsChecked(i)) {
      columns[i] = m_panel->m_lWeatherRoutes->GetColumnCount();
      wxString name = _(column_names[i]);

      if (i == STARTTIME || i == ENDTIME) {
        name += _T(" (");
        if (m_SettingsDialog.m_cbUseLocalTime->GetValue())
          name += _("local");
        else
          name += _T("UTC");
        name += _T(")");
      }

      m_panel->m_lWeatherRoutes->InsertColumn(columns[i], name);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[i], wxLIST_AUTOSIZE);
    } else
      columns[i] = -1;
  }

  std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++, it++) {
    m_panel->m_lWeatherRoutes->SetItemPtrData(i, (wxUIntPtr)*it);
    (*it)->Update(
        this);  // update utc/local switch to strings of start/end time
    UpdateItem(i);
  }

  OnWeatherRouteSelected();  // update utc/local switch if configuration dialog
                             // is visible
}

static void CursorPositionDialogMessage(CursorPositionDialog& dlg,
                                        wxString msg) {
  dlg.m_stPosition->SetLabel(msg);
  dlg.m_stPosition->Fit();
  dlg.m_stTime->SetLabel("");
  dlg.m_stPolar->SetLabel("");
  dlg.m_stSailChanges->SetLabel("");
  dlg.m_stTacks->SetLabel("");
  dlg.m_stJibes->SetLabel("");
  dlg.m_stSailPlanChanges->SetLabel("");
  dlg.m_stWeatherData->SetLabel("");
  dlg.Fit();
}

static void RoutePositionDialogMessage(RoutePositionDialog& dlg, wxString msg) {
  dlg.m_stPosition->SetLabel(msg);
  dlg.m_stPosition->Fit();
  dlg.m_stTime->SetLabel("");
  dlg.m_stPolar->SetLabel("");
  dlg.m_stSailChanges->SetLabel("");
  dlg.m_stTacks->SetLabel("");
  dlg.m_stJibes->SetLabel("");
  dlg.m_stSailPlanChanges->SetLabel("");
  dlg.m_stWeatherData->SetLabel("");
  dlg.Fit();
}

void WeatherRouting::UpdateCursorPositionDialog() {
  CursorPositionDialog& dlg = m_CursorPositionDialog;
  if (!dlg.IsShown()) return;

  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  if (currentroutemaps.size() != 1) {
    CursorPositionDialogMessage(dlg, _("Select exactly 1 configuration"));
    return;
  }

  RouteMapOverlay* rmo = currentroutemaps.front();
  Position* p = rmo->GetLastCursorPosition();
  if (!p) {
    CursorPositionDialogMessage(dlg, _("Cursor outside computed route map"));
    return;
  }
  wxDateTime display_time = rmo->GetLastCursorTime();

  if (m_SettingsDialog.m_cbUseLocalTime->GetValue())
    display_time = display_time.FromUTC();

  dlg.m_stTime->SetLabel(display_time.Format(_T("%x %H:%M")));

  RouteMapConfiguration configuration = rmo->GetConfiguration();
  auto latStr = toSDMM_PlugIn(NEflag::LAT, p->lat, Precision::HI);
  auto lonStr = toSDMM_PlugIn(NEflag::LON, p->lon, Precision::HI);
  dlg.m_stPosition->SetLabel(latStr + " " + lonStr);

  if (p->polar == -1)
    dlg.m_stPolar->SetLabel(wxEmptyString);
  else {
    wxFileName fn = configuration.boat.Polars[p->polar].FileName;
    dlg.m_stPolar->SetLabel(fn.GetFullName());
  }

  dlg.m_stSailChanges->SetLabel(wxString::Format(_T("%d"), p->SailChanges()));

  dlg.m_stTacks->SetLabel(wxString::Format(_T("%d"), p->tacks));
  dlg.m_stJibes->SetLabel(wxString::Format(_T("%d"), p->jibes));
  dlg.m_stSailPlanChanges->SetLabel(
      wxString::Format(_T("%d"), p->sail_plan_changes));

  wxString weatherdata;
  wxString grib = _("Grib") + _T(" ");
  wxString climatology = _("Climatology") + _T(" ");
  wxString data_deficient = _("Data Deficient") + _T(" ");
  wxString wind = _("Wind") + _T(" ");
  wxString current = _("Current") + _T(" ");

  if (p->data_mask & Position::GRIB_WIND) weatherdata += grib + wind;
  if (p->data_mask & Position::CLIMATOLOGY_WIND)
    weatherdata += climatology + wind;
  if (p->data_mask & Position::DATA_DEFICIENT_WIND)
    weatherdata += data_deficient + wind;
  if (p->data_mask & Position::GRIB_CURRENT) weatherdata += grib + current;
  if (p->data_mask & Position::CLIMATOLOGY_CURRENT)
    weatherdata += climatology + current;
  if (p->data_mask & Position::DATA_DEFICIENT_CURRENT)
    weatherdata += data_deficient + current;

  dlg.m_stWeatherData->SetLabel(weatherdata);
  dlg.Fit();
}

void WeatherRouting::UpdateRoutePositionDialog() {
  /* New method to display information on the weather route
   * (like time, duration, position, wind, speed, etc.)
   * based on cursor position of the user.
   * This is complementary with the plot chart.
   */

  RoutePositionDialog& dlg = m_RoutePositionDialog;
  if (!dlg.IsShown()) return;

  m_positionOnRoute = nullptr;
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  if (currentroutemaps.size() != 1) {
    RoutePositionDialogMessage(dlg, _("Select exactly 1 configuration"));
    return;
  }

  RouteMapOverlay* rmo = currentroutemaps.front();
  RouteMapConfiguration configuration = rmo->GetConfiguration();

  // CUSTOMIZATION
  // -------------------------
  // Get access to the closest point between the cursor location
  // and the weather routing computed point.
  PlotData data;
  Position* closestPosition = rmo->getClosestRoutePositionFromCursor(
      m_weather_routing_pi.m_cursor_lat, m_weather_routing_pi.m_cursor_lon,
      data);

  // Store position to display it
  m_positionOnRoute = closestPosition;
  if (!closestPosition && data.time.IsValid()) {
    m_positionOnRoute = &m_savedPosition;
    m_savedPosition = data;
  }

  if (!m_positionOnRoute) {
    RoutePositionDialogMessage(dlg, _("Cursor outside computed route map"));
    return;
  }

  // TRIP DURATION
  wxDateTime startTime = configuration.StartTime;
  wxDateTime cursorTime = data.time;

  if (m_SettingsDialog.m_cbUseLocalTime->GetValue()) {
    startTime = startTime.FromUTC();
    cursorTime = data.time.FromUTC();
  }
  dlg.m_stTime->SetLabel(cursorTime.Format(_T("%x %H:%M")));

  wxString duration = calculateTimeDelta(startTime, cursorTime);
  dlg.m_stDuration->SetLabel(duration);

  // POSITION
  auto latStr = toSDMM_PlugIn(NEflag::LAT, data.lat, Precision::HI);
  auto lonStr = toSDMM_PlugIn(NEflag::LON, data.lon, Precision::HI);
  dlg.m_stPosition->SetLabel(latStr + _T(" ") + lonStr);

  // POLAR
  if (data.polar == -1)
    dlg.m_stPolar->SetLabel(wxEmptyString);
  else {
    wxFileName fn = configuration.boat.Polars[data.polar].FileName;
    dlg.m_stPolar->SetLabel(fn.GetFullName());
  }

  // TACKS
  dlg.m_stTacks->SetLabel(wxString::Format(_T("%d"), data.tacks));
  // JIBES
  dlg.m_stJibes->SetLabel(wxString::Format(_T("%d"), data.jibes));

  // BOAT SPEED
  if (std::abs(data.stw - data.sog) > 0.1) {
    dlg.m_stBoatSpeed->SetLabel(wxString::Format(
        _T("%.1f knts (SOW), %.1f knts (SOG)"), data.stw, data.sog));
  } else {
    dlg.m_stBoatSpeed->SetLabel(wxString::Format(_T("%.1f knts"), data.stw));
  }

  // BEARING
  if (std::abs(data.ctw - data.cog) >= 5) {
    dlg.m_stBoatCourse->SetLabel(wxString::Format(
        _T("%.0f \u00B0T (COW), %.0f \u00B0T (COG)"),
        positive_degrees(data.ctw), positive_degrees(data.cog)));
  } else {
    dlg.m_stBoatCourse->SetLabel(
        wxString::Format(_T("%.0f \u00B0T"), positive_degrees(data.ctw)));
  }

  // WIND SPEED
  // RouteInfo(RouteMapOverlay::COMFORT);
  dlg.m_stTWS->SetLabel(wxString::Format(_T("%.0f knts"), data.twsOverWater));

  // WIND: TRUE WIND ANGLE
  // For wind direction, specify if it is
  // coming from starboard or port side.
  double windDirection = heading_resolve(data.ctw - data.twdOverWater);
  wxString windDirectionLabel;
  if (windDirection <= 0)
    windDirectionLabel =
        wxString::Format(_T("%.0f\u00B0 starboard"), fabs(windDirection));
  else
    windDirectionLabel =
        wxString::Format(_T("%.0f\u00B0 port"), fabs(windDirection));
  dlg.m_stTWA->SetLabel(windDirectionLabel);

  // WIND: APPARENT WIND SPEED
  float apparentWindSpeed =
      Polar::VelocityApparentWind(data.stw, windDirection, data.twsOverWater);
  dlg.m_stAWS->SetLabel(wxString::Format(_T("%.0f knts"), apparentWindSpeed));

  // WIND: APPARENT WIND SPEED
  float apparentWindDirection = Polar::DirectionApparentWind(
      apparentWindSpeed, data.stw, windDirection, data.twsOverWater);
  wxString apparentWindDirectionLabel;
  if (apparentWindDirection <= 0)
    apparentWindDirectionLabel = wxString::Format(_T("%.0f\u00B0 starboard"),
                                                  fabs(apparentWindDirection));
  else
    apparentWindDirectionLabel =
        wxString::Format(_T("%.0f\u00B0 port"), fabs(apparentWindDirection));
  dlg.m_stAWA->SetLabel(apparentWindDirectionLabel);

  // WAVES
  dlg.m_stWaves->SetLabel(wxString::Format(_T("%.0f m"), data.WVHT));

  // WIND GUST
  dlg.m_stWindGust->SetLabel(wxString::Format(_T("%.0f knts"), data.VW_GUST));

  // CLIMATOLOGY DATA
  wxString weatherdata;
  wxString grib = _("Grib") + _T(" ");
  wxString climatology = _("Climatology") + _T(" ");
  wxString data_deficient = _("Data Deficient") + _T(" ");
  wxString wind = _("Wind") + _T(" ");
  wxString current = _("Current") + _T(" ");
  if (closestPosition) {
    // SAIL CHANGES
    dlg.m_stSailChanges->SetLabel(
        wxString::Format(_T("%d"), closestPosition->SailChanges()));

    if (closestPosition->data_mask & Position::GRIB_WIND)
      weatherdata += grib + wind;
    if (closestPosition->data_mask & Position::CLIMATOLOGY_WIND)
      weatherdata += climatology + wind;
    if (closestPosition->data_mask & Position::DATA_DEFICIENT_WIND)
      weatherdata += data_deficient + wind;
    if (closestPosition->data_mask & Position::GRIB_CURRENT)
      weatherdata += grib + current;
    if (closestPosition->data_mask & Position::CLIMATOLOGY_CURRENT)
      weatherdata += climatology + current;
    if (closestPosition->data_mask & Position::DATA_DEFICIENT_CURRENT)
      weatherdata += data_deficient + current;

    dlg.m_stWeatherData->SetLabel(weatherdata);
  }
  dlg.Fit();
}

void WeatherRouting::OnNewPosition(wxCommandEvent& event) {
  NewPositionDialog dlg(this);
  if (dlg.ShowModal() == wxID_OK) {
    double lat = 0, lon = 0, lat_minutes = 0, lon_minutes = 0;

    wxString latitude_degrees = dlg.m_tLatitudeDegrees->GetValue();
    wxString latitude_minutes = dlg.m_tLatitudeMinutes->GetValue();
    latitude_degrees.ToDouble(&lat);
    latitude_minutes.ToDouble(&lat_minutes);
    lat_minutes = fabs(lat_minutes);
    if (lat < 0) lat_minutes = -lat_minutes;
    lat += lat_minutes / 60;

    wxString longitude_degrees = dlg.m_tLongitudeDegrees->GetValue();
    wxString longitude_minutes = dlg.m_tLongitudeMinutes->GetValue();
    longitude_degrees.ToDouble(&lon);
    longitude_minutes.ToDouble(&lon_minutes);
    lon_minutes = fabs(lon_minutes);
    if (lon < 0) lon_minutes = -lon_minutes;
    lon += lon_minutes / 60;

    AddPosition(lat, lon, dlg.m_tName->GetValue());
  }
}

void WeatherRouting::OnUpdateBoat(wxCommandEvent& event) {
  double lat = m_weather_routing_pi.m_boat_lat;
  double lon = m_weather_routing_pi.m_boat_lon;

  long index = 0;
  for (std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
       it != RouteMap::Positions.end(); it++, index++)
    if ((*it).Name == _("Boat")) {
      m_panel->m_lPositions->SetItem(
          index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
      m_panel->m_lPositions->SetItem(
          index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));

      (*it).lat = lat, (*it).lon = lon;
      UpdateConfigurations();
      return;
    }

  AddPosition(lat, lon, _("Boat"));
}

#if 0 /* wx widgets is shit, can only allow users \
         to edit the first column, so this doesn't work */
void WeatherRouting::OnListLabelEdit( wxListEvent& event )
{
    long index = event.GetIndex();
    int col = event.GetColumn();
    
    long i = 0;
    for(std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
        it != RouteMap::Positions.end(); it++, i++)
        if(i == index) {
            if(col == POSITION_NAME) {
                (*it).Name = event.GetText(); 
            } else {
                double value;
                event.GetText().ToDouble(&value);
                if(col == POSITION_LAT)
                    (*it).lat = value;
                else if(col == POSITION_LON)
                    (*it).lon = value;

                m_lPositions->SetItem(index, col, wxString::Format(_T("%.5f"), value));
                UpdateConfigurations();
            }
        }
}
#endif

void WeatherRouting::OnDeletePosition(wxCommandEvent& event) {
  long index = m_panel->m_lPositions->GetNextItem(-1, wxLIST_NEXT_ALL,
                                                  wxLIST_STATE_SELECTED);
  if (index < 0) return;

  wxListItem item;
  item.SetId(index);
  item.SetColumn(0);
  item.SetMask(
      wxLIST_MASK_TEXT);  // Note use of the mask, somehow it's required for
                          // this to work correctly on windows
  m_panel->m_lPositions->GetItem(item);

  long ID = m_panel->m_lPositions->GetItemData(index);
  assert(ID >= 0);

  for (std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
       it != RouteMap::Positions.end(); it++) {
    if ((*it).ID == ID) {
      wxString name = (*it).Name;
      m_ConfigurationDialog.RemoveSource(name);
      m_ConfigurationBatchDialog.RemoveSource(name);
      RouteMap::Positions.erase(it);
      break;
    }
  }
  m_panel->m_lPositions->DeleteItem(index);

  UpdateConfigurations();
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnDeleteAllPositions(wxCommandEvent& event) {
  RouteMap::Positions.clear();
  m_ConfigurationDialog.ClearSources();
  m_ConfigurationBatchDialog.ClearSources();
  m_panel->m_lPositions->DeleteAllItems();
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnPositionKeyDown(wxListEvent& event) {
  switch (event.GetKeyCode()) {
    case WXK_DELETE: {
      wxCommandEvent event;
      OnDeletePosition(event);
    } break;
    default:
      event.Skip();
  }
}

void WeatherRouting::OnEditConfiguration() {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);
  if (routemapoverlays.empty()) return;

  m_ApplyingMultiLegGroupSettings = false;
  m_MultiLegSettingsGroupId.Clear();
  m_MultiLegLegSnapshots.clear();

  m_ConfigurationDialog.Show();

#if 0
    /* if boat filename doesn't exist open boat dialog immediately */
    wxString boatfilename = m_ConfigurationDialog.m_fpBoat->GetPath();
    if(!boatfilename.empty() && !wxFileName::FileExists(boatfilename))
        m_ConfigurationDialog.EditBoat();
#endif
}

void WeatherRouting::OnGoTo(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps(true);
  if (currentroutemaps.empty()) return;

  double avg_lat = 0, avg_lonx = 0, avg_lony = 0, total = 0;
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();
    if (std::isnan(configuration.StartLat)) continue;
    avg_lat += configuration.StartLat + configuration.EndLat;
    avg_lonx = cos(deg2rad(configuration.StartLon)) +
               cos(deg2rad(configuration.EndLon));
    avg_lony = sin(deg2rad(configuration.StartLon)) +
               sin(deg2rad(configuration.EndLon));

    total += 2;
  }

  avg_lat /= total, avg_lonx /= total, avg_lony /= total;
  double avg_lon = rad2deg(atan2(avg_lony, avg_lonx));

  double max_distance = 0;
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();
    if (std::isnan(configuration.StartLat)) continue;
    double distance;
    DistanceBearingMercator_Plugin(avg_lat, avg_lon, configuration.StartLat,
                                   configuration.StartLon, NULL, &distance);
    max_distance = wxMax(distance, max_distance);
    DistanceBearingMercator_Plugin(avg_lat, avg_lon, configuration.EndLat,
                                   configuration.EndLon, NULL, &distance);
    max_distance = wxMax(distance, max_distance);
  }

  if (max_distance > 1e-4)
    JumpToPosition(avg_lat, avg_lon, .125 / max_distance);
  else {
    wxMessageDialog mdlg(this, _("Cannot goto invalid route(s)."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
}

void WeatherRouting::OnDelete(wxCommandEvent& event) {
  //  Stop all computations to avoid thread corruption.
  //  Probably could do better to stop only the computation of selected
  //  configuration But stopping all is safer. Sess:
  //  https://github.com/rgleason/weather_routing_pi/issues/103
  StopAll();

  long index = m_panel->m_lWeatherRoutes->GetNextItem(-1, wxLIST_NEXT_ALL,
                                                      wxLIST_STATE_SELECTED);
  if (index < 0) return;

  DeleteRouteMaps(CurrentRouteMaps());

  /* select map just after the first one selected */
  int cnt = m_panel->m_lWeatherRoutes->GetItemCount();
  m_panel->m_lWeatherRoutes->SetItemState(index == cnt ? index - 1 : index,
                                          wxLIST_STATE_SELECTED,
                                          wxLIST_STATE_SELECTED);
  GetParent()->Refresh();

  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnDeleteAll(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> allroutemapoverlays;
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    allroutemapoverlays.push_back(weatherroute->routemapoverlay);
  }

  DeleteRouteMaps(allroutemapoverlays);

  GetParent()->Refresh();

  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnWeatherRouteSort(wxListEvent& event) {
  sortcol = event.GetColumn();
  sortorder = -sortorder;

  if (sortcol == 0) {
    for (int index = 0; index < m_panel->m_lWeatherRoutes->GetItemCount();
         index++) {
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
      weatherroute->routemapoverlay->m_bEndRouteVisible = sortorder == 1;
      UpdateItem(index);
    }
    RequestRefresh(GetParent());
  } else {
#if wxCHECK_VERSION(2, 9, 0)
    m_panel->m_lWeatherRoutes->SortItems(SortWeatherRoutes,
                                         (wxIntPtr)m_panel->m_lWeatherRoutes);
#else
    m_panel->m_lWeatherRoutes->SortItems(SortWeatherRoutes,
                                         (long)m_panel->m_lWeatherRoutes);
#endif
  }
}

void WeatherRouting::OnWeatherRouteSelected() {
  GetParent()->Refresh();

  // Get the list of currently selected routes.
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  std::list<RouteMapConfiguration> currentconfigurations;
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    (*it)->SetCursorLatLon(m_weather_routing_pi.m_cursor_lat,
                           m_weather_routing_pi.m_cursor_lon);
    currentconfigurations.push_back((*it)->GetConfiguration());
  }

  if (currentroutemaps.empty())
    m_tHideConfiguration.Start(25, true);
  else {
    m_tHideConfiguration.Stop();
    m_bSkipUpdateCurrentItems = true;
    m_ConfigurationDialog.SetConfigurations(currentconfigurations);
    m_bSkipUpdateCurrentItems = false;
  }

  UpdateDialogs();

  // Update the Routing Table panel if it exists and is shown
  if (m_RoutingTablePanel) {
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    wxAuiPaneInfo& pane = pauimgr->GetPane(m_RoutingTablePanel);
    if (pane.IsOk() && pane.IsShown()) {
      if (!currentroutemaps.empty()) {
        // Update with the first selected route
        ((RoutingTablePanel*)m_RoutingTablePanel)->m_RouteMap =
            currentroutemaps.front();
        ((RoutingTablePanel*)m_RoutingTablePanel)->PopulateTable();
      }
    }
  }

  SetEnableConfigurationMenu();
}

void WeatherRouting::OnWeatherRouteKeyDown(wxListEvent& event) {
  switch (event.GetKeyCode()) {
    case WXK_DELETE: {
      wxCommandEvent event;
      OnDelete(event);
    } break;
    default:
      event.Skip();
  }
}

void WeatherRouting::OnWeatherRoutesListLeftDown(wxMouseEvent& event) {
  OnLeftDown(event);
  wxPoint pos = event.GetPosition();
  int flags = 0;
  long index = m_panel->m_lWeatherRoutes->HitTest(pos, flags);

  // Do we have the Visibility column?
  if (columns[VISIBLE] >= 0) {
    int minx = 0,
        maxx = m_panel->m_lWeatherRoutes->GetColumnWidth(columns[VISIBLE]);

    //    Clicking Visibility column?
    if (index >= 0 && event.GetX() >= minx && event.GetX() < maxx) {
      // Process the clicked item
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
      weatherroute->routemapoverlay->m_bEndRouteVisible =
          !weatherroute->routemapoverlay->m_bEndRouteVisible;
      UpdateItem(index);
      RequestRefresh(GetParent());
    }
  }

  // Allow wx to process...
  event.Skip();
}

void WeatherRouting::UpdateComputeState() {
  m_panel->m_gProgress->SetRange(m_RoutesToRun);

  if (m_bRunning) return;

  m_bRunning = true;
  m_panel->m_gProgress->SetValue(0);

  m_mCompute->Enable();
  m_panel->m_bCompute->Enable();
  m_StartTime = wxDateTime::Now();
  m_tCompute.Start(1, true);
}

class DepartureTimeOptimizationResultsDialog : public wxDialog {
public:
  DepartureTimeOptimizationResultsDialog(
      WeatherRouting* parent, const std::list<RouteMapOverlay*>& routemaps,
      const wxDateTime& nominalStartTime)
      : wxDialog(parent, wxID_ANY, _("Departure Time Optimisation Results"),
                 wxDefaultPosition, wxSize(1100, 420),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_WeatherRouting(parent),
        m_RouteMaps(routemaps),
        m_NominalStartTime(nominalStartTime),
        m_AutoRefreshTimer(this),
        m_AutoRefreshCount(0) {
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    m_List = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);

    wxString columns[] = {_("Best"),       _("Offset"),      _("Departure"),
                          _("ETA"),        _("Elapsed"),     _("Distance"),
                          _("Avg Speed"),  _("Avg SOG"),     _("Max SOG"),
                          _("Avg Wind"),   _("Max Wind"),    _("Avg Current"),
                          _("Max Current"), _("Tacks"),      _("Comfort"),
                          _("State")};
    for (unsigned int i = 0; i < WXSIZEOF(columns); i++)
      m_List->InsertColumn(i, columns[i]);

    topSizer->Add(m_List, 1, wxEXPAND | wxALL, 5);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* refresh = new wxButton(this, wxID_REFRESH, _("Refresh"));
    buttonSizer->Add(refresh, 0, wxALL, 5);
    buttonSizer->AddStretchSpacer();
    wxButton* close = new wxButton(this, wxID_CLOSE, _("Close"));
    buttonSizer->Add(close, 0, wxALL, 5);
    topSizer->Add(buttonSizer, 0, wxEXPAND);

    SetSizer(topSizer);
    refresh->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent&) {
                    Populate();
                    UpdateAutoRefresh();
                  });
    close->Bind(wxEVT_BUTTON,
                [this](wxCommandEvent&) {
                  StopAutoRefresh();
                  EndModal(wxID_OK);
                });
    Bind(wxEVT_TIMER, &DepartureTimeOptimizationResultsDialog::OnAutoRefresh,
         this);
    Bind(wxEVT_CLOSE_WINDOW,
         [this](wxCloseEvent&) {
           StopAutoRefresh();
           EndModal(wxID_OK);
         });
    Populate();
    UpdateAutoRefresh();
  }

  ~DepartureTimeOptimizationResultsDialog() override { StopAutoRefresh(); }

private:
  static const int AUTO_REFRESH_INTERVAL_MS = 1000;
  static const int AUTO_REFRESH_MAX_COUNT = 3600;

  WeatherRoute* FindWeatherRoute(RouteMapOverlay* routemap) {
    for (auto it = m_WeatherRouting->m_WeatherRoutes.begin();
         it != m_WeatherRouting->m_WeatherRoutes.end(); it++)
      if ((*it)->routemapoverlay == routemap) return *it;
    return NULL;
  }

  bool RouteMapIsInList(RouteMapOverlay* routemap,
                        const std::list<RouteMapOverlay*>& routemaps) const {
    return std::find(routemaps.begin(), routemaps.end(), routemap) !=
           routemaps.end();
  }

  bool RouteStillPending(RouteMapOverlay* routemap) {
    if (!m_WeatherRouting || !FindWeatherRoute(routemap)) return false;
    if (RouteMapIsInList(routemap, m_WeatherRouting->m_WaitingRouteMaps))
      return true;
    if (RouteMapIsInList(routemap, m_WeatherRouting->m_RunningRouteMaps))
      return true;
    return routemap->Running();
  }

  bool HasPendingRoutes() {
    for (auto routemap : m_RouteMaps)
      if (RouteStillPending(routemap)) return true;
    return false;
  }

  void StopAutoRefresh() {
    if (m_AutoRefreshTimer.IsRunning()) m_AutoRefreshTimer.Stop();
  }

  void UpdateAutoRefresh() {
    if (HasPendingRoutes() && m_AutoRefreshCount < AUTO_REFRESH_MAX_COUNT) {
      if (!m_AutoRefreshTimer.IsRunning())
        m_AutoRefreshTimer.Start(AUTO_REFRESH_INTERVAL_MS);
    } else {
      StopAutoRefresh();
    }
  }

  void OnAutoRefresh(wxTimerEvent&) {
    m_AutoRefreshCount++;
    Populate();
    UpdateAutoRefresh();
  }

  wxString FormatOffset(int minutes) {
    wxString sign = minutes < 0 ? _T("-") : _T("+");
    int absMinutes = abs(minutes);
    return wxString::Format(_T("%s%d:%02d"), sign, absMinutes / 60,
                            absMinutes % 60);
  }

  wxString FormatElapsedSeconds(long seconds) {
    int days = seconds / 86400;
    seconds %= 86400;
    int hours = seconds / 3600;
    seconds %= 3600;
    int minutes = seconds / 60;
    if (days)
      return wxString::Format(_T("%dd %02d:%02d"), days, hours, minutes);
    return wxString::Format(_T("%02d:%02d"), hours, minutes);
  }

  bool IsFiniteMetricText(wxString value) const {
    value.Trim(true);
    value.Trim(false);
    value.MakeLower();
    return value.Find(_T("nan")) == wxNOT_FOUND &&
           value.Find(_T("inf")) == wxNOT_FOUND;
  }

  wxString MetricOrNA(const wxString& value, bool complete) const {
    if (!complete) return _("N/A");
    if (!IsFiniteMetricText(value)) return _("N/A");
    if (value.IsEmpty()) return _("N/A");
    return value;
  }

  void SetCell(long row, int col, const wxString& value) {
    m_List->SetItem(row, col, value);
    m_List->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
  }

  void Populate() {
    m_List->DeleteAllItems();

    RouteMapOverlay* bestRoute = NULL;
    long bestSeconds = std::numeric_limits<long>::max();
    for (auto routemap : m_RouteMaps) {
      RouteMapConfiguration configuration = routemap->GetConfiguration();
      if (!routemap->Finished() || !routemap->ReachedDestination()) continue;

      wxTimeSpan elapsed = routemap->EndTime() - configuration.StartTime;
      long seconds = elapsed.GetSeconds().ToLong();
      if (seconds >= 0 && seconds < bestSeconds) {
        bestSeconds = seconds;
        bestRoute = routemap;
      }
    }

    for (auto routemap : m_RouteMaps) {
      WeatherRoute* weatherroute = FindWeatherRoute(routemap);
      if (!weatherroute) continue;
      weatherroute->Update(m_WeatherRouting);

      RouteMapConfiguration configuration = routemap->GetConfiguration();
      long row = m_List->InsertItem(m_List->GetItemCount(),
                                    routemap == bestRoute ? _("Best") : _T(""));
      bool complete = routemap->Finished() && routemap->ReachedDestination();
      SetCell(row, 1,
              FormatOffset(configuration.DepartureTimeOptimizationOffsetMinutes));
      SetCell(row, 2, weatherroute->StartTime);
      SetCell(row, 3, complete ? weatherroute->EndTime : _("N/A"));

      if (complete) {
        wxTimeSpan elapsed = routemap->EndTime() - configuration.StartTime;
        SetCell(row, 4,
                FormatElapsedSeconds(elapsed.GetSeconds().ToLong()));
      } else {
        SetCell(row, 4, _("N/A"));
      }

      SetCell(row, 5, MetricOrNA(weatherroute->Distance, complete));
      SetCell(row, 6, MetricOrNA(weatherroute->AvgSpeed, complete));
      SetCell(row, 7, MetricOrNA(weatherroute->AvgSpeedGround, complete));
      SetCell(row, 8, MetricOrNA(weatherroute->MaxSpeedGround, complete));
      SetCell(row, 9, MetricOrNA(weatherroute->AvgWind, complete));
      SetCell(row, 10, MetricOrNA(weatherroute->MaxWind, complete));
      SetCell(row, 11, MetricOrNA(weatherroute->AvgCurrent, complete));
      SetCell(row, 12, MetricOrNA(weatherroute->MaxCurrent, complete));
      SetCell(row, 13, MetricOrNA(weatherroute->Tacks, complete));
      SetCell(row, 14, MetricOrNA(weatherroute->Comfort, complete));
      SetCell(row, 15, weatherroute->State);
    }
  }

  WeatherRouting* m_WeatherRouting;
  std::list<RouteMapOverlay*> m_RouteMaps;
  wxDateTime m_NominalStartTime;
  wxListCtrl* m_List;
  wxTimer m_AutoRefreshTimer;
  int m_AutoRefreshCount;
};

void WeatherRouting::ShowDepartureTimeOptimizationResults(
    const std::list<RouteMapOverlay*>& routemapoverlays,
    const wxDateTime& nominalStartTime) {
  DepartureTimeOptimizationResultsDialog dialog(this, routemapoverlays,
                                                nominalStartTime);
  dialog.ShowModal();
}

class MultiLegDepartureOptimizationResultsDialog : public wxDialog {
public:
  explicit MultiLegDepartureOptimizationResultsDialog(WeatherRouting* parent)
      : wxDialog(parent, wxID_ANY,
                 _("Multi-leg Departure Time Optimisation Results"),
                 wxDefaultPosition, wxSize(1150, 420),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_WeatherRouting(parent),
        m_AutoRefreshTimer(this),
        m_AutoRefreshCount(0) {
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    m_List = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);

    wxString columns[] = {_("Best"),      _("Offset"),       _("Departure"),
                          _("Final ETA"), _("Total Time"),   _("Distance"),
                          _("Legs"),      _("State"),        _("Reason")};
    for (unsigned int i = 0; i < WXSIZEOF(columns); i++)
      m_List->InsertColumn(i, columns[i]);

    topSizer->Add(m_List, 1, wxEXPAND | wxALL, 5);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* refresh = new wxButton(this, wxID_REFRESH, _("Refresh"));
    buttonSizer->Add(refresh, 0, wxALL, 5);
    wxButton* applyBest = new wxButton(this, wxID_ANY, _("Apply Best"));
    buttonSizer->Add(applyBest, 0, wxALL, 5);
    wxButton* applySelected =
        new wxButton(this, wxID_ANY, _("Apply Selected"));
    buttonSizer->Add(applySelected, 0, wxALL, 5);
    buttonSizer->AddStretchSpacer();
    wxButton* discard =
        new wxButton(this, wxID_ANY, _("Discard Candidates"));
    buttonSizer->Add(discard, 0, wxALL, 5);
    wxButton* close = new wxButton(this, wxID_CLOSE, _("Close"));
    buttonSizer->Add(close, 0, wxALL, 5);
    topSizer->Add(buttonSizer, 0, wxEXPAND);

    SetSizer(topSizer);
    refresh->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent&) {
                    Populate();
                    UpdateAutoRefresh();
                  });
    applyBest->Bind(wxEVT_BUTTON,
                    [this](wxCommandEvent&) {
                      ApplyBest();
                    });
    applySelected->Bind(wxEVT_BUTTON,
                        [this](wxCommandEvent&) {
                          ApplySelected();
                        });
    discard->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent&) {
                    DiscardAndClose();
                  });
    close->Bind(wxEVT_BUTTON,
                [this](wxCommandEvent&) {
                  CloseDialog();
                });
    Bind(wxEVT_TIMER, &MultiLegDepartureOptimizationResultsDialog::OnAutoRefresh,
         this);
    Bind(wxEVT_CLOSE_WINDOW,
         [this](wxCloseEvent&) {
           CloseDialog();
         });
    Populate();
    UpdateAutoRefresh();
  }

  ~MultiLegDepartureOptimizationResultsDialog() override { StopAutoRefresh(); }

private:
  static const int AUTO_REFRESH_INTERVAL_MS = 1000;
  static const int AUTO_REFRESH_MAX_COUNT = 7200;

  int SelectedCandidateIndex() const {
    long selected = m_List->GetNextItem(-1, wxLIST_NEXT_ALL,
                                        wxLIST_STATE_SELECTED);
    return selected == -1 ? -1 : (int)selected;
  }

  bool ApplyBest() {
    if (!m_WeatherRouting) return false;
    bool applied = m_WeatherRouting->ApplyBestMultiLegOptimizationCandidate();
    Populate();
    UpdateAutoRefresh();
    return applied;
  }

  bool ApplySelected() {
    if (!m_WeatherRouting) return false;
    int index = SelectedCandidateIndex();
    if (index < 0) {
      wxMessageBox(_("Select a complete candidate first."), _("Weather Routing"),
                   wxOK | wxICON_WARNING, this);
      return false;
    }
    bool applied = m_WeatherRouting->ApplyMultiLegOptimizationCandidate(index);
    Populate();
    UpdateAutoRefresh();
    return applied;
  }

  void DiscardAndClose() {
    StopAutoRefresh();
    if (m_WeatherRouting)
      m_WeatherRouting->CloseMultiLegDepartureOptimizationResults();
    EndModal(wxID_OK);
  }

  void CloseDialog() {
    if (m_WeatherRouting &&
        m_WeatherRouting->MultiLegDepartureOptimizationActive()) {
      wxMessageDialog confirm(
          this,
          _("Multi-leg departure optimisation is still running. Cancel it and "
            "discard temporary candidate legs?"),
          _("Weather Routing"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
      if (confirm.ShowModal() != wxID_YES) return;
      DiscardAndClose();
      return;
    }

    if (m_WeatherRouting &&
        m_WeatherRouting->AppliedMultiLegOptimizationCandidateIndex() < 0 &&
        m_WeatherRouting->HasCompleteMultiLegOptimizationCandidate()) {
      wxMessageDialog confirm(
          this,
          _("Apply the best complete multi-leg departure candidate before "
            "closing?\n\nYes: apply best candidate\nNo: discard temporary "
            "candidate legs\nCancel: return to results"),
          _("Weather Routing"),
          wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
      int answer = confirm.ShowModal();
      if (answer == wxID_CANCEL) return;
      if (answer == wxID_YES && !ApplyBest()) return;
    }

    StopAutoRefresh();
    if (m_WeatherRouting)
      m_WeatherRouting->CloseMultiLegDepartureOptimizationResults();
    EndModal(wxID_OK);
  }

  void StopAutoRefresh() {
    if (m_AutoRefreshTimer.IsRunning()) m_AutoRefreshTimer.Stop();
  }

  void UpdateAutoRefresh() {
    if (m_WeatherRouting &&
        m_WeatherRouting->MultiLegDepartureOptimizationActive() &&
        m_AutoRefreshCount < AUTO_REFRESH_MAX_COUNT) {
      if (!m_AutoRefreshTimer.IsRunning())
        m_AutoRefreshTimer.Start(AUTO_REFRESH_INTERVAL_MS);
    } else {
      StopAutoRefresh();
    }
  }

  void OnAutoRefresh(wxTimerEvent&) {
    m_AutoRefreshCount++;
    Populate();
    UpdateAutoRefresh();
  }

  wxString FormatOffset(int minutes) const {
    wxString sign = minutes < 0 ? _T("-") : _T("+");
    int absMinutes = abs(minutes);
    return wxString::Format(_T("%s%d:%02d"), sign, absMinutes / 60,
                            absMinutes % 60);
  }

  wxString FormatElapsedSeconds(long seconds) const {
    if (seconds < 0) return _("N/A");
    int days = seconds / 86400;
    seconds %= 86400;
    int hours = seconds / 3600;
    seconds %= 3600;
    int minutes = seconds / 60;
    if (days)
      return wxString::Format(_T("%dd %02d:%02d"), days, hours, minutes);
    return wxString::Format(_T("%02d:%02d"), hours, minutes);
  }

  wxString FormatTime(wxDateTime time) const {
    if (!time.IsValid()) return _("N/A");
    if (m_WeatherRouting &&
        m_WeatherRouting->m_SettingsDialog.m_cbUseLocalTime->GetValue())
      time = time.FromUTC();
    return time.Format(_T("%x %H:%M"));
  }

  void SetCell(long row, int col, const wxString& value) {
    m_List->SetItem(row, col, value);
    m_List->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
  }

  void Populate() {
    m_List->DeleteAllItems();
    if (!m_WeatherRouting) return;

    const auto& candidates = m_WeatherRouting->MultiLegOptimizationCandidates();
    for (const auto& candidate : candidates) {
      wxString marker;
      if (candidate.applied)
        marker = _("Applied");
      else if (candidate.best)
        marker = _("Best");
      long row = m_List->InsertItem(m_List->GetItemCount(), marker);
      SetCell(row, 1, FormatOffset(candidate.offsetMinutes));
      SetCell(row, 2, FormatTime(candidate.departureTime));
      SetCell(row, 3,
              candidate.complete ? FormatTime(candidate.finalEta) : _("N/A"));
      SetCell(row, 4, FormatElapsedSeconds(candidate.totalElapsedSeconds));
      SetCell(row, 5,
              candidate.complete
                  ? wxString::Format(_T("%.0f NM"), candidate.totalDistance)
                  : _("N/A"));
      SetCell(row, 6,
              wxString::Format(_T("%d/%d"), candidate.completedLegs,
                               candidate.totalLegs));
      SetCell(row, 7, candidate.state);
      wxString reason = candidate.reason;
      if (!candidate.failedLegName.IsEmpty()) {
        if (!reason.IsEmpty()) reason = candidate.failedLegName + _T(": ") + reason;
        else reason = candidate.failedLegName;
      }
      SetCell(row, 8, reason);
    }
  }

  WeatherRouting* m_WeatherRouting;
  wxListCtrl* m_List;
  wxTimer m_AutoRefreshTimer;
  int m_AutoRefreshCount;
};

void WeatherRouting::ShowMultiLegDepartureOptimizationResults() {
  MultiLegDepartureOptimizationResultsDialog dialog(this);
  dialog.ShowModal();
}

bool WeatherRouting::ComputeDepartureTimeOptimization(
    RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration base = routemapoverlay->GetConfiguration();
  if (!base.DepartureTimeOptimizationEnabled ||
      base.DepartureTimeOptimizationCandidate)
    return false;

  int rangeMinutes = base.DepartureTimeOptimizationRangeMinutes;
  int stepMinutes = base.DepartureTimeOptimizationStepMinutes;
  if (rangeMinutes < 0) {
    wxMessageDialog mdlg(this, _("Departure optimisation range must not be "
                                 "negative."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return true;
  }
  if (stepMinutes <= 0) {
    wxMessageDialog mdlg(this, _("Departure optimisation step must be greater "
                                 "than zero."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return true;
  }

  std::vector<int> offsets;
  for (int offset = -rangeMinutes; offset <= rangeMinutes;
       offset += stepMinutes)
    offsets.push_back(offset);
  offsets.push_back(0);
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

  if (offsets.size() > MAX_DEPARTURE_OPTIMIZATION_CANDIDATES) {
    wxMessageDialog mdlg(
        this,
        wxString::Format(
            _("Departure optimisation would create %d route calculations. "
              "Increase the step or reduce the range. The current limit is "
              "%d."),
            (int)offsets.size(), MAX_DEPARTURE_OPTIMIZATION_CANDIDATES),
        _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return true;
  }

  std::list<RouteMapOverlay*> oldOptimizationRoutes;
  for (auto it = m_WeatherRoutes.begin(); it != m_WeatherRoutes.end(); it++) {
    RouteMapConfiguration configuration =
        (*it)->routemapoverlay->GetConfiguration();
    if (configuration.DepartureTimeOptimizationCandidate)
      oldOptimizationRoutes.push_back((*it)->routemapoverlay);
  }
  for (auto routemap : oldOptimizationRoutes)
    if (routemap) Stop(routemap);
  DeleteRouteMaps(oldOptimizationRoutes);
  m_DepartureOptimizationRoutes.clear();

  wxString groupId = wxDateTime::UNow().FormatISOCombined();
  wxDateTime nominalStartTime = base.StartTime;
  for (auto offset : offsets) {
    RouteMapConfiguration candidate = base;
    candidate.DepartureTimeOptimizationEnabled = false;
    candidate.DepartureTimeOptimizationCandidate = true;
    candidate.DepartureTimeOptimizationNominalStartTime = nominalStartTime;
    candidate.DepartureTimeOptimizationOffsetMinutes = offset;
    candidate.DepartureTimeOptimizationGroupId = groupId;
    candidate.StartTime = nominalStartTime + wxTimeSpan::Minutes(offset);

    if (!AddConfiguration(candidate)) continue;
    RouteMapOverlay* candidateRoute = m_WeatherRoutes.back()->routemapoverlay;
    candidateRoute->LoadBoat();
    m_DepartureOptimizationRoutes.push_back(candidateRoute);
    Start(candidateRoute);
  }

  return true;
}

void WeatherRouting::OnCompute(wxCommandEvent& event) {
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  wxDateTime optimizationNominalStartTime;
  bool showOptimizationResults = false;
  for (auto it = currentroutemaps.begin(); it != currentroutemaps.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();
    if (ComputeDepartureTimeOptimization(*it)) {
      optimizationNominalStartTime = configuration.StartTime;
      showOptimizationResults = true;
    } else
      Start(*it);
  }
  UpdateComputeState();
  if (showOptimizationResults && !m_DepartureOptimizationRoutes.empty())
    ShowDepartureTimeOptimizationResults(m_DepartureOptimizationRoutes,
                                         optimizationNominalStartTime);
}

void WeatherRouting::OnComputeMultiLegSequence(wxCommandEvent& event) {
  CancelMultiLegDepartureOptimization(true);
  RouteMapOverlay* selected = FirstCurrentRouteMap();
  if (!selected) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return;
  }
  ComputeMultiLegSequence(selected);
}

void WeatherRouting::OnOptimizeMultiLegDeparture(wxCommandEvent& event) {
  RouteMapOverlay* selected = FirstCurrentRouteMap();
  if (!selected) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return;
  }
  ComputeMultiLegDepartureOptimization(selected);
}

void WeatherRouting::OnEditMultiLegGroupSettings(wxCommandEvent& event) {
  RouteMapOverlay* selected = FirstCurrentRouteMap();
  if (!selected) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return;
  }
  EditMultiLegGroupSettings(selected);
}

void WeatherRouting::OnShowRoutingStatus(wxCommandEvent& event) {
  ShowRoutingStatus(FirstCurrentRouteMap());
}

void WeatherRouting::OnComputeAll(wxCommandEvent& event) {
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  StartAll();
  UpdateComputeState();
}

void WeatherRouting::OnStop(wxCommandEvent& event) {
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  for (auto it = currentroutemaps.begin(); it != currentroutemaps.end(); it++)
    Stop(*it);
  UpdateComputeState();
}

#define FAIL(X)  \
  do {           \
    error = X;   \
    goto failed; \
  } while (0)
void WeatherRouting::OnOpen(wxCommandEvent& event) {
  wxString error;
  wxFileDialog openDialog(
      this, _("Select Configuration"), m_FileName.GetPath(),
      m_FileName.GetName(),
      wxT("XML files (*.xml)|*.XML;*.xml|All files (*.*)|*.*"), wxFD_OPEN);

  if (openDialog.ShowModal() == wxID_OK) {
    wxCommandEvent event;
    OnDeleteAllPositions(event);
    OnDeleteAll(event);
    OpenXML(openDialog.GetPath());
  }
}

void WeatherRouting::OnSave(wxCommandEvent& event) {
  if (m_FileName.GetFullPath().IsEmpty()) {
    // No file path set yet, behave like Save As
    OnSaveAs(event);
    return;
  }

  SaveXML(m_FileName.GetFullPath());
  m_tAutoSaveXML
      .Stop();  // Stop any pending auto-save since we just manually saved
}

void WeatherRouting::OnSaveAs(wxCommandEvent& event) {
  wxString error;
  wxFileDialog saveDialog(
      this, _("Select Configuration"), m_FileName.GetPath(),
      m_FileName.GetName(),
      wxT("XML files (*.xml)|*.XML;*.xml|All files (*.*)|*.*"),
      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

  if (saveDialog.ShowModal() == wxID_OK) {
    // Use wxFileDialog::AppendExtension to ensure the file has the .xml
    // extension
    wxString filename =
        wxFileDialog::AppendExtension(saveDialog.GetPath(), _T("*.xml"));

    SaveXML(filename);
    m_tAutoSaveXML
        .Stop();  // Stop any pending auto-save since we just manually saved
  }
}

void WeatherRouting::OnClose(wxCommandEvent& event) { Hide(); }

void WeatherRouting::OnCollPaneChanged(wxCollapsiblePaneEvent& event) {
  if (m_colpane && m_colpane->IsExpanded())
    SetSize(m_size);
  else if (m_colpane)
    Fit();
  Update();
  Layout();
}

void WeatherRouting::OnSize(wxSizeEvent& event) {
  if (m_colpane && m_colpane->IsExpanded()) {
    Update();
    Layout();
    m_size = GetSize();
  } else {
    if (m_colpane) Fit();
  }
  event.Skip();
}

void WeatherRouting::OnNew(wxCommandEvent& event) {
  RouteMapConfiguration configuration;
  if (FirstCurrentRouteMap())
    configuration = FirstCurrentRouteMap()->GetConfiguration();
  else
    configuration = DefaultConfiguration();

  AddConfiguration(configuration);

  // deselect all
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++)
    m_panel->m_lWeatherRoutes->SetItemState(i, 0, wxLIST_STATE_SELECTED);

  m_panel->m_lWeatherRoutes->SetItemState(
      m_panel->m_lWeatherRoutes->GetItemCount() - 1, wxLIST_STATE_SELECTED,
      wxLIST_STATE_SELECTED);
  OnEditConfiguration();
}

void WeatherRouting::OnBatch(wxCommandEvent& event) {
  if (m_ConfigurationBatchDialog.IsShown()) return;

  m_ConfigurationBatchDialog.Reset();
  m_ConfigurationBatchDialog.Show();
}

void WeatherRouting::GenerateBatch() {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);

  wxProgressDialog* progressdialog = NULL;
  int count = routemapoverlays.size(), c = 0;
  int times = 0;

  wxTimeSpan StartSpan, StartSpacingSpan;
  double days, hours;

  ConfigurationBatchDialog& dlg = m_ConfigurationBatchDialog;
  dlg.m_tStartDays->GetValue().ToDouble(&days);
  StartSpan = wxTimeSpan::Days(days);

  dlg.m_tStartHours->GetValue().ToDouble(&hours);
  StartSpan += wxTimeSpan::Seconds(3600 * hours);

  dlg.m_tStartSpacingDays->GetValue().ToDouble(&days);
  StartSpacingSpan = wxTimeSpan::Days(days);

  dlg.m_tStartSpacingHours->GetValue().ToDouble(&hours);
  StartSpacingSpan += wxTimeSpan::Seconds(3600 * hours);

  if (!StartSpacingSpan.GetSeconds().ToLong()) {
    wxMessageDialog mdlg(this, _("Zero time span forbidden, aborting."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return;
  }

  wxDateTime StartTime = wxDateTime::Now(), EndTime = StartTime + StartSpan;

  for (wxDateTime start = StartTime; start <= EndTime;
       start += StartSpacingSpan)
    times++;

  int sources = 0;
  for (std::vector<BatchSource*>::iterator it = dlg.sources.begin();
       it != dlg.sources.end(); it++)
    for (std::list<BatchDestination*>::iterator it2 =
             (*it)->destinations.begin();
         it2 != (*it)->destinations.end(); it2++)
      sources++;

  count *= sources;
  count *= dlg.m_lBoats->GetCount();

  if (count > 10) {
    progressdialog = new wxProgressDialog(
        _("Batch configuration"), _("Weather Routing"), count, this,
        wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
  }

  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();

    EndTime = configuration.StartTime + StartSpan;

    for (; configuration.StartTime <= EndTime;
         configuration.StartTime += StartSpacingSpan) {
      for (std::vector<BatchSource*>::iterator it = dlg.sources.begin();
           it != dlg.sources.end(); it++) {
        configuration.Start = (*it)->Name;

        for (std::list<BatchDestination*>::iterator it2 =
                 (*it)->destinations.begin();
             it2 != (*it)->destinations.end(); it2++) {
          configuration.End = (*it2)->Name;

          for (unsigned int boatindex = 0; boatindex < dlg.m_lBoats->GetCount();
               boatindex++) {
            configuration.boatFileName = dlg.m_lBoats->GetString(boatindex);

            for (int windstrength = dlg.m_sWindStrengthMin->GetValue();
                 windstrength <= dlg.m_sWindStrengthMax->GetValue();
                 windstrength += dlg.m_sWindStrengthStep->GetValue()) {
              configuration.WindStrength = windstrength / 100.0;

              AddConfiguration(configuration);
              m_WeatherRoutes.back()->routemapoverlay->LoadBoat();
              configuration =
                  m_WeatherRoutes.back()->routemapoverlay->GetConfiguration();

              if (progressdialog && !progressdialog->Update(c++)) goto abort;
            }
          }
        }
      }
    }
  }
abort:
  DeleteRouteMaps(routemapoverlays);

  delete progressdialog;
}

bool WeatherRouting::Show(bool show) {
  m_weather_routing_pi.ShowMenuItems(show);

  if (show) {
    m_ConfigurationDialog.Show(m_bShowConfiguration);
    m_ConfigurationBatchDialog.Show(m_bShowConfigurationBatch);
    m_SettingsDialog.Show(m_bShowSettings);
    m_StatisticsDialog.Show(m_bShowStatistics);
    m_ReportDialog.Show(m_bShowReport);
    m_PlotDialog.Show(m_bShowPlot);
    m_FilterRoutesDialog.Show(m_bShowFilter);
    m_RoutePositionDialog.Show(m_bShowRoutePosition);
  } else {
    m_bShowConfiguration = m_ConfigurationDialog.IsShown();
    m_ConfigurationDialog.Hide();

    m_bShowConfigurationBatch = m_ConfigurationBatchDialog.IsShown();
    m_ConfigurationBatchDialog.Hide();

    m_bShowSettings = m_SettingsDialog.IsShown();
    m_SettingsDialog.Hide();

    m_bShowStatistics = m_StatisticsDialog.IsShown();
    m_StatisticsDialog.Hide();

    m_bShowReport = m_ReportDialog.IsShown();
    m_ReportDialog.Hide();

    m_bShowPlot = m_PlotDialog.IsShown();
    m_PlotDialog.Hide();

    m_bShowFilter = m_FilterRoutesDialog.IsShown();
    m_FilterRoutesDialog.Hide();

    m_bShowRoutePosition = m_RoutePositionDialog.IsShown();
    m_RoutePositionDialog.Hide();

    // Hide routing table panel if it exists
    if (m_RoutingTablePanel) {
      wxAuiManager* pauimgr = ::GetFrameAuiManager();
      wxAuiPaneInfo& pane = pauimgr->GetPane(m_RoutingTablePanel);
      if (pane.IsOk() && pane.IsShown()) pane.Hide();
      pauimgr->Update();
    }
  }

  return WeatherRoutingBase::Show(show);
}

void WeatherRouting::OnFilter(wxCommandEvent& event) {
  m_FilterRoutesDialog.Show();
}

void WeatherRouting::OnResetAll(wxCommandEvent& event) {
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  m_StatisticsDialog.SetRunTime(m_RunTime = wxTimeSpan(0));
  Reset();
  UpdateStates();
}

void WeatherRouting::OnSaveAsTrack(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++)
    SaveAsTrack(**it);
}

void WeatherRouting::OnSaveAsRoute(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++)
    SaveAsRoute(**it);
}

void WeatherRouting::OnExportRouteAsGPX(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);
  int nfail = 0;
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++) {
    std::list<PlotData> plotdata = (*it)->GetPlotData(false);

    if (plotdata.empty())
      nfail++;
    else
      ExportRoute(**it);
  }
  if (nfail) {
    wxString nc;
    nc.Printf("%d ", nfail);
    wxString msg(_("Route export failed"));
    msg += "\n";
    msg += nc;
    msg += _("Route(s) not computed, cannot export");
    wxMessageDialog mdlg(this, msg, _("Weather Routing"),
                         wxOK | wxICON_WARNING);
    mdlg.ShowModal();
  }
}

void WeatherRouting::OnSaveAllAsTracks(wxCommandEvent& event) {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++)
    SaveAsTrack(*reinterpret_cast<WeatherRoute*>(
                     wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)))
                     ->routemapoverlay);
}

void WeatherRouting::OnSettings(wxCommandEvent& event) {
  m_SettingsDialog.Show();
}

void WeatherRouting::OnStatistics(wxCommandEvent& event) {
  m_StatisticsDialog.SetRouteMapOverlays(CurrentRouteMaps());
  m_StatisticsDialog.Show();
}

void WeatherRouting::OnReport(wxCommandEvent& event) {
  m_ReportDialog.SetRouteMapOverlays(CurrentRouteMaps());
  m_ReportDialog.Show();
}

void WeatherRouting::OnPlot(wxCommandEvent& event) {
  m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());
  m_PlotDialog.Show();
}

void WeatherRouting::OnCursorPosition(wxCommandEvent& event) {
  m_CursorPositionDialog.Show(!m_CursorPositionDialog.IsShown());
  UpdateCursorPositionDialog();
}

// CUSTOMIZATION
void WeatherRouting::OnRoutePosition(wxCommandEvent& event) {
  m_RoutePositionDialog.Show(!m_RoutePositionDialog.IsShown());
  UpdateRoutePositionDialog();
}

void WeatherRouting::AddRoutingPanel() {
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps(true);
  if (currentroutemaps.empty()) return;

  if (!m_RoutingTablePanel) {
    // Create the panel if it doesn't exist yet
    wxWindow* parent = m_weather_routing_pi.GetParentWindow();

    // Create the panel directly with the updated RoutingTablePanel
    m_RoutingTablePanel =
        new RoutingTablePanel(parent, *this, currentroutemaps.front());

    // Add the panel to the AUI manager
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    wxAuiPaneInfo pane = wxAuiPaneInfo()
                             .Name(_T("Weather Routing Table"))
                             .Caption(_T("Weather Routing Table"))
                             .CaptionVisible(true)
                             .Float()
                             .FloatingPosition(100, 100)
                             .FloatingSize(700, 400)
                             .Dockable(true)
                             .Movable(true)
                             .CloseButton(true);

    pauimgr->AddPane(m_RoutingTablePanel, pane);

#if OCPN_API_VERSION_MAJOR > 1 || \
    (OCPN_API_VERSION_MAJOR == 1 && OCPN_API_VERSION_MINOR >= 20)
    // Set color scheme using GetAppColorScheme() from the OpenCPN API
    PI_ColorScheme cs = GetAppColorScheme();
    ((RoutingTablePanel*)m_RoutingTablePanel)->SetColorScheme(cs);
#endif

    pauimgr->Update();
  } else {
    // Update data in existing panel
    ((RoutingTablePanel*)m_RoutingTablePanel)->m_RouteMap =
        currentroutemaps.front();
    ((RoutingTablePanel*)m_RoutingTablePanel)->PopulateTable();

    // Show the panel if it's hidden
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    wxAuiPaneInfo& pane = pauimgr->GetPane(m_RoutingTablePanel);
    if (!pane.IsShown()) {
      pane.Show(true);
      pauimgr->Update();
    }
  }
}

void WeatherRouting::OnWeatherTable(wxCommandEvent& event) {
  AddRoutingPanel();
}

void WeatherRouting::OnManual(wxCommandEvent& event) {
  wxLaunchDefaultBrowser(
      "https://opencpn.org/wiki/dokuwiki/"
      "doku.php?id=opencpn:opencpn_user_manual:plugins:weather:weather_"
      "routing");
}

void WeatherRouting::OnInformation(wxCommandEvent& event) {
  wxString infolocation = GetPluginDataDir("weather_routing_pi") +
                          _T("/data/") + _("WeatherRoutingInformation.html");
  wxLaunchDefaultBrowser(_T("file://") + infolocation);
}

void WeatherRouting::OnAbout(wxCommandEvent& event) {
  AboutDialog dlg(GetParent());
  dlg.ShowModal();
}

void WeatherRouting::OnComputationTimer(wxTimerEvent&) {
  std::vector<RouteMapOverlay*> completedRouteMaps;
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end();) {
    RouteMapOverlay* routemapoverlay = *it;
    if (!routemapoverlay->Running()) {
      routemapoverlay->DeleteThread();

      it = m_RunningRouteMaps.erase(it);

      m_panel->m_gProgress->SetValue(m_RoutesToRun - m_WaitingRouteMaps.size() -
                                     m_RunningRouteMaps.size());
      UpdateRouteMap(routemapoverlay);
      completedRouteMaps.push_back(routemapoverlay);

      /* update report if needed */
      m_ReportDialog.m_bReportStale = true;
      if (m_ReportDialog.IsShown()) {
        std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps();
        for (std::list<RouteMapOverlay*>::iterator it =
                 routemapoverlays.begin();
             it != routemapoverlays.end(); it++)
          if (routemapoverlay == *it) {
            m_ReportDialog.SetRouteMapOverlays(routemapoverlays);
            break;
          }
      }

      continue;
    } else
      it++;

    /* get a new grib for the route map if needed */
    if (routemapoverlay->NeedsGrib() && !routemapoverlay->Finished()) {
      m_RouteMapOverlayNeedingGrib = routemapoverlay;
      routemapoverlay->RequestGrib(routemapoverlay->NewTime());
      m_RouteMapOverlayNeedingGrib = NULL;
    }
  }

  for (auto routemapoverlay : completedRouteMaps) {
    AdvanceMultiLegSequence(routemapoverlay);
    AdvanceMultiLegDepartureOptimization(routemapoverlay);
  }

  if ((int)m_RunningRouteMaps.size() <
          m_SettingsDialog.m_sConcurrentThreads->GetValue() &&
      m_WaitingRouteMaps.size()) {
    RouteMapOverlay* routemapoverlay = m_WaitingRouteMaps.front();
    m_WaitingRouteMaps.pop_front();
    wxString error;
    if (routemapoverlay->Start(error))
      m_RunningRouteMaps.push_back(routemapoverlay);
    else {
      wxMessageDialog mdlg(this, _("Failed to start configuration: ") + error,
                           _("Weather Routing"), wxOK | wxICON_ERROR);
      mdlg.ShowModal();
    }

    UpdateRouteMap(routemapoverlay);
  }

  static int cycles; /* don't refresh all the time */
  if (++cycles > 50 || !m_RunningRouteMaps.size()) {
    cycles = 0;

    std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
    for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
         it != currentroutemaps.end(); it++)
      if ((*it)->Updated()) {
        m_StatisticsDialog.SetRunTime(m_RunTime +=
                                      wxDateTime::Now() - m_StartTime);
        if (m_StatisticsDialog.IsShown())
          m_StatisticsDialog.SetRouteMapOverlays(CurrentRouteMaps());
        if (m_PlotDialog.IsShown())
          m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());

        m_StartTime = wxDateTime::Now();
        GetParent()->Refresh();
        break;
      }
  }

  if (m_RunningRouteMaps.size()) {
    /* todo, instead of respawning the funky timer here,
       maybe we can do it from the thread instead to eliminate the delay */
    m_tCompute.Start(25, true);
    return;
  }

  StopAll();
}

void WeatherRouting::OnHideConfigurationTimer(wxTimerEvent&) {
  m_ConfigurationDialog.Hide();
}

void WeatherRouting::OnAutoSaveXMLTimer(wxTimerEvent&) { AutoSaveXML(); }

void WeatherRouting::AutoSaveXML() { SaveXML(m_FileName.GetFullPath()); }

void WeatherRouting::OnRenderedTimer(wxTimerEvent&) {
  // don't do it until the window system is up and running
  if (GetClientSize().GetWidth() > 20) {
    if (!sashpos) sashpos = GetClientSize().GetWidth() / 5;
    m_panel->m_splitter1->SetSashPosition(sashpos, true);
    Disconnect(wxEVT_IDLE, wxTimerEventHandler(WeatherRouting::OnRenderedTimer),
               NULL, this);
  }
}

bool WeatherRouting::OpenXML(wxString filename, bool reportfailure) {
  TiXmlDocument doc;
  wxString error;

  wxFileName fn(filename);
  SetTitle(_("Weather Routing") + wxString(_T(" - ")) + fn.GetFullName());
  m_FileName = fn;

  wxProgressDialog* progressdialog = NULL;
  wxDateTime start = wxDateTime::UNow();

  wxString lastboatFileName;
  Boat lastboat;

  if (!doc.LoadFile(filename.mb_str()))
    FAIL(_("Failed to load file."));
  else {
    TiXmlHandle root(doc.RootElement());

    if (strcmp(root.Element()->Value(), "OpenCPNWeatherRoutingConfiguration"))
      FAIL(_("Invalid xml file"));

    RouteMap::Positions.clear();

    int count = 0;
    for (TiXmlElement* e = root.FirstChild().Element(); e;
         e = e->NextSiblingElement())
      count++;

    int i = 0;
    for (TiXmlElement* e = root.FirstChild().Element(); e;
         e = e->NextSiblingElement(), i++) {
      if (progressdialog) {
        if (!progressdialog->Update(i)) {
          delete progressdialog;
          return true;
        }
      } else {
        wxDateTime now = wxDateTime::UNow();
        /* if it's going to take more than a half second, show a progress dialog
         */
        if ((now - start).GetMilliseconds() > 250 && i < count / 2) {
          progressdialog = new wxProgressDialog(
              _("Load"), _("Weather Routing"), count, this,
              wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
        }
      }

      if (!strcmp(e->Value(), "Position")) {
        wxString name = wxString::FromUTF8(e->Attribute("Name"));
        wxString GUID = wxString::FromUTF8(e->Attribute("GUID"));
        double lat = AttributeDouble(e, "Latitude", NAN);
        double lon = AttributeDouble(e, "Longitude", NAN);

        if (GUID.IsEmpty())
          for (auto it : RouteMap::Positions) {
            if (it.Name == name) {
              static bool warnonce = true;
              if (warnonce) {
                warnonce = false;
                wxMessageDialog mdlg(
                    this,
                    _("File contains duplicate position name, discaring\n"),
                    _("Weather Routing"), wxOK | wxICON_WARNING);
                mdlg.ShowModal();
              }

              goto skipadd;
            }
          }
        AddPosition(lat, lon, name, GUID);

      skipadd:;
      } else if (!strcmp(e->Value(), "Configuration")) {
        // Ideally the name of the XML element should be "Routings" but it is
        // kept as "Configuration" for backward compatibility.
        RouteMapConfiguration configuration;
        configuration.RouteGUID = wxString::FromUTF8(e->Attribute("GUID"));
        configuration.StartType =
            (RouteMapConfiguration::StartDataType)AttributeInt(
                e, "StartType", RouteMapConfiguration::START_FROM_POSITION);
        configuration.Start = wxString::FromUTF8(e->Attribute("Start"));
        configuration.EndType =
            (RouteMapConfiguration::EndDataType)AttributeInt(
                e, "EndType", RouteMapConfiguration::END_AT_POSITION);
        const char* startGuid = e->Attribute("StartGUID");
        if (startGuid) configuration.StartGUID = wxString::FromUTF8(startGuid);
        const char* endGuid = e->Attribute("EndGUID");
        if (endGuid) configuration.EndGUID = wxString::FromUTF8(endGuid);
        if (configuration.StartType ==
                RouteMapConfiguration::START_FROM_POSITION &&
            !configuration.StartGUID.IsEmpty())
          configuration.StartType = RouteMapConfiguration::START_FROM_WAYPOINT;
        if (configuration.EndType == RouteMapConfiguration::END_AT_POSITION &&
            !configuration.EndGUID.IsEmpty())
          configuration.EndType = RouteMapConfiguration::END_AT_WAYPOINT;
        configuration.UseCurrentTime =
            AttributeBool(e, "UseCurrentTime", false);
        configuration.DepartureTimeOptimizationEnabled = AttributeBool(
            e, "DepartureTimeOptimizationEnabled", false);
        configuration.DepartureTimeOptimizationRangeMinutes =
            AttributeInt(e, "DepartureTimeOptimizationRangeMinutes", 360);
        configuration.DepartureTimeOptimizationStepMinutes =
            AttributeInt(e, "DepartureTimeOptimizationStepMinutes", 60);
        configuration.IsMultiLegGenerated =
            AttributeBool(e, "IsMultiLegGenerated", false);
        const char* multiLegGroupId = e->Attribute("MultiLegGroupId");
        if (multiLegGroupId)
          configuration.MultiLegGroupId =
              wxString::FromUTF8(multiLegGroupId);
        const char* multiLegParentRouteGUID =
            e->Attribute("MultiLegParentRouteGUID");
        if (multiLegParentRouteGUID)
          configuration.MultiLegParentRouteGUID =
              wxString::FromUTF8(multiLegParentRouteGUID);
        const char* multiLegParentRouteName =
            e->Attribute("MultiLegParentRouteName");
        if (multiLegParentRouteName)
          configuration.MultiLegParentRouteName =
              wxString::FromUTF8(multiLegParentRouteName);
        configuration.MultiLegLegIndex =
            AttributeInt(e, "MultiLegLegIndex", 0);
        configuration.MultiLegLegCount =
            AttributeInt(e, "MultiLegLegCount", 0);
        if (configuration.UseCurrentTime) {
          // The current time will be overridden when the route is computed.
          configuration.StartTime = wxDateTime::Now().ToUTC();
        } else {
          wxDateTime date;
          date.ParseISODate(wxString::FromUTF8(e->Attribute("StartDate")));
          wxDateTime time;
          time.ParseISOTime(wxString::FromUTF8(e->Attribute("StartTime")));
          if (date.IsValid()) {
            if (time.IsValid()) {
              date.SetHour(time.GetHour());
              date.SetMinute(time.GetMinute());
              date.SetSecond(time.GetSecond());
            }
            configuration.StartTime = date;
          } else {
            configuration.StartTime = wxDateTime::Now();
          }
        }

        configuration.End = wxString::FromUTF8(e->Attribute("End"));
        configuration.DeltaTime = AttributeDouble(e, "dt", 0);

        configuration.boatFileName = wxString::FromUTF8(e->Attribute("Boat"));
        if (!wxFileName::FileExists(configuration.boatFileName)) {
          configuration.boatFileName =
              weather_routing_pi::StandardPath() + _T("boats") +
              wxFileName::GetPathSeparator() + configuration.boatFileName;
          if (!wxFileName::FileExists(configuration.boatFileName)) {
            configuration.boatFileName = _T("");
          }
        }

        configuration.Integrator =
            (RouteMapConfiguration::IntegratorType)AttributeInt(e, "Integrator",
                                                                0);

        configuration.MaxDivertedCourse =
            AttributeDouble(e, "MaxDivertedCourse", 90);
        configuration.MaxCourseAngle =
            AttributeDouble(e, "MaxCourseAngle", 180);
        configuration.MaxSearchAngle =
            AttributeDouble(e, "MaxSearchAngle", 120);
        configuration.MaxTrueWindKnots =
            AttributeDouble(e, "MaxTrueWindKnots", 50);
        configuration.MaxApparentWindKnots =
            AttributeDouble(e, "MaxApparentWindKnots", 50);

        configuration.MaxSwellMeters =
            AttributeDouble(e, "MaxSwellMeters", 20.);
        configuration.MaxLatitude = AttributeDouble(e, "MaxLatitude", 90);
        configuration.TackingTime = AttributeDouble(e, "TackingTime", 0);
        configuration.JibingTime = AttributeDouble(e, "JibingTime", 0);
        configuration.SailPlanChangeTime =
            AttributeDouble(e, "SailPlanChangeTime", 0);
        configuration.WindVSCurrent = AttributeDouble(e, "WindVSCurrent", 0);

        configuration.AvoidCycloneTracks =
            AttributeBool(e, "AvoidCycloneTracks", false);
        configuration.CycloneMonths = AttributeInt(e, "CycloneMonths", 2);
        configuration.CycloneDays = AttributeInt(e, "CycloneDays", 0);

        configuration.UseGrib = AttributeBool(e, "UseGrib", true);
        configuration.ClimatologyType =
            (RouteMapConfiguration::ClimatologyDataType)AttributeInt(
                e, "ClimatologyType", RouteMapConfiguration::CUMULATIVE_MAP);
        configuration.AllowDataDeficient =
            AttributeBool(e, "AllowDataDeficient", false);
        configuration.WindStrength = AttributeDouble(e, "WindStrength", 1);

        configuration.UpwindEfficiency =
            AttributeDouble(e, "UpwindEfficiency", 1.);
        configuration.DownwindEfficiency =
            AttributeDouble(e, "DownwindEfficiency", 1.);
        configuration.NightCumulativeEfficiency =
            AttributeDouble(e, "NightCumulativeEfficiency", 1.);

        configuration.DetectLand = AttributeBool(e, "DetectLand", true);
        configuration.SafetyMarginLand =
            AttributeDouble(e, "SafetyMarginLand", 0.);
        configuration.DetectBoundary =
            AttributeBool(e, "DetectBoundary", false);
        configuration.Currents = AttributeBool(e, "Currents", true);
        configuration.OptimizeTacking =
            AttributeBool(e, "OptimizeTacking", false);

        configuration.InvertedRegions =
            AttributeBool(e, "InvertedRegions", false);
        configuration.Anchoring = AttributeBool(e, "Anchoring", false);

        configuration.FromDegree = AttributeDouble(e, "FromDegree", 0);
        configuration.ToDegree = AttributeDouble(e, "ToDegree", 180);
        configuration.ByDegrees = AttributeDouble(e, "ByDegrees", 5.);

        if (configuration.boatFileName == lastboatFileName)
          configuration.boat = lastboat;

        AddConfiguration(configuration);

        lastboatFileName = configuration.boatFileName;
        m_WeatherRoutes.back()->routemapoverlay->LoadBoat();
        lastboat =
            m_WeatherRoutes.back()->routemapoverlay->GetConfiguration().boat;
      } else
        FAIL(_("Unrecognized xml node"));
    }
  }

  delete progressdialog;
  return true;
failed:

  delete progressdialog;
  if (reportfailure) {
    wxMessageDialog mdlg(this, error, _("Weather Routing"),
                         wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
  return false;
}

void WeatherRouting::SaveXML(wxString filename) {
  wxFileName fn(filename);
  SetTitle(_("Weather Routing") + wxString(_T(" - ")) + fn.GetFullName());
  m_FileName = fn;

  TiXmlDocument doc;
  TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "utf-8", "");
  doc.LinkEndChild(decl);

  TiXmlElement* root = new TiXmlElement("OpenCPNWeatherRoutingConfiguration");
  doc.LinkEndChild(root);

  char version[24];
  sprintf(version, "%d.%d", PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR);
  root->SetAttribute("version", version);
  root->SetAttribute("creator", "Opencpn Weather Routing plugin");

  for (std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
       it != RouteMap::Positions.end(); it++) {
    TiXmlElement* c = new TiXmlElement("Position");

    c->SetAttribute("Name", (*it).Name.mb_str());
    c->SetAttribute("Latitude",
                    wxString::Format(_T("%.5f"), (*it).lat).mb_str());
    c->SetAttribute("Longitude",
                    wxString::Format(_T("%.5f"), (*it).lon).mb_str());
    if (!(*it).GUID.IsEmpty()) c->SetAttribute("GUID", (*it).GUID.mb_str());

    root->LinkEndChild(c);
  }

  for (auto it = m_WeatherRoutes.begin(); it != m_WeatherRoutes.end(); it++) {
    // Ideally the name of the XML element should be "Routings" but it is kept
    // as "Configuration" for backward compatibility.
    TiXmlElement* c = new TiXmlElement("Configuration");

    RouteMapConfiguration configuration =
        (*it)->routemapoverlay->GetConfiguration();
    if (configuration.DepartureTimeOptimizationCandidate) continue;

    if (!configuration.RouteGUID.IsEmpty())
      c->SetAttribute("GUID", configuration.RouteGUID.mb_str());

    c->SetAttribute("StartType", configuration.StartType);
    c->SetAttribute("Start", configuration.Start.mb_str());
    if (!configuration.StartGUID.IsEmpty())
      c->SetAttribute("StartGUID", configuration.StartGUID.mb_str());
    c->SetAttribute("EndType", configuration.EndType);
    c->SetAttribute("UseCurrentTime", configuration.UseCurrentTime);
    c->SetAttribute("DepartureTimeOptimizationEnabled",
                    configuration.DepartureTimeOptimizationEnabled);
    c->SetAttribute("DepartureTimeOptimizationRangeMinutes",
                    configuration.DepartureTimeOptimizationRangeMinutes);
    c->SetAttribute("DepartureTimeOptimizationStepMinutes",
                    configuration.DepartureTimeOptimizationStepMinutes);
    c->SetAttribute("IsMultiLegGenerated", configuration.IsMultiLegGenerated);
    if (!configuration.MultiLegGroupId.IsEmpty())
      c->SetAttribute("MultiLegGroupId",
                      configuration.MultiLegGroupId.mb_str());
    if (!configuration.MultiLegParentRouteGUID.IsEmpty())
      c->SetAttribute("MultiLegParentRouteGUID",
                      configuration.MultiLegParentRouteGUID.mb_str());
    if (!configuration.MultiLegParentRouteName.IsEmpty())
      c->SetAttribute("MultiLegParentRouteName",
                      configuration.MultiLegParentRouteName.mb_str());
    if (configuration.MultiLegLegIndex)
      c->SetAttribute("MultiLegLegIndex", configuration.MultiLegLegIndex);
    if (configuration.MultiLegLegCount)
      c->SetAttribute("MultiLegLegCount", configuration.MultiLegLegCount);
    if (!configuration.UseCurrentTime) {
      c->SetAttribute("StartDate",
                      configuration.StartTime.FormatISODate().mb_str());
      c->SetAttribute("StartTime",
                      configuration.StartTime.FormatISOTime().mb_str());
    }
    c->SetAttribute("End", configuration.End.mb_str());
    if (!configuration.EndGUID.IsEmpty())
      c->SetAttribute("EndGUID", configuration.EndGUID.mb_str());
    c->SetAttribute("dt", configuration.DeltaTime);

    c->SetAttribute("Boat", configuration.boatFileName.ToUTF8());

    c->SetAttribute("Integrator", configuration.Integrator);

    c->SetAttribute("MaxDivertedCourse", configuration.MaxDivertedCourse);
    c->SetAttribute("MaxCourseAngle", configuration.MaxCourseAngle);
    c->SetAttribute("MaxSearchAngle", configuration.MaxSearchAngle);
    c->SetAttribute("MaxTrueWindKnots", configuration.MaxTrueWindKnots);
    c->SetAttribute("MaxApparentWindKnots", configuration.MaxApparentWindKnots);

    c->SetDoubleAttribute("MaxSwellMeters", configuration.MaxSwellMeters);
    c->SetAttribute("MaxLatitude", configuration.MaxLatitude);
    c->SetAttribute("TackingTime", configuration.TackingTime);
    c->SetAttribute("JibingTime", configuration.JibingTime);
    c->SetAttribute("SailPlanChangeTime", configuration.SailPlanChangeTime);
    c->SetAttribute("WindVSCurrent", configuration.WindVSCurrent);

    c->SetAttribute("AvoidCycloneTracks", configuration.AvoidCycloneTracks);
    c->SetAttribute("CycloneMonths", configuration.CycloneMonths);
    c->SetAttribute("CycloneDays", configuration.CycloneDays);

    c->SetAttribute("UseGrib", configuration.UseGrib);
    c->SetAttribute("ClimatologyType", configuration.ClimatologyType);
    c->SetAttribute("AllowDataDeficient", configuration.AllowDataDeficient);
    c->SetDoubleAttribute("WindStrength", configuration.WindStrength);

    c->SetDoubleAttribute("UpwindEfficiency", configuration.UpwindEfficiency);
    c->SetDoubleAttribute("DownwindEfficiency",
                          configuration.DownwindEfficiency);
    c->SetDoubleAttribute("NightCumulativeEfficiency",
                          configuration.NightCumulativeEfficiency);

    c->SetAttribute("DetectLand", configuration.DetectLand);
    c->SetDoubleAttribute("SafetyMarginLand", configuration.SafetyMarginLand);
    c->SetAttribute("DetectBoundary", configuration.DetectBoundary);
    c->SetAttribute("Currents", configuration.Currents);
    c->SetAttribute("OptimizeTacking", configuration.OptimizeTacking);

    c->SetAttribute("InvertedRegions", configuration.InvertedRegions);
    c->SetAttribute("Anchoring", configuration.Anchoring);

    c->SetDoubleAttribute("FromDegree", configuration.FromDegree);
    c->SetDoubleAttribute("ToDegree", configuration.ToDegree);
    c->SetDoubleAttribute("ByDegrees", configuration.ByDegrees);

    root->LinkEndChild(c);
  }

  if (!doc.SaveFile(filename.mb_str())) {
    wxMessageDialog mdlg(this, _("Failed to save xml file: ") + filename,
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
}

void WeatherRouting::SetEnableConfigurationMenu() {
  bool current = FirstCurrentRouteMap() != NULL;
  m_mBatch->Enable(current);
  m_mBatch1->Enable(current);
  m_mEdit->Enable(current);
  m_mEdit1->Enable(current);
  m_mEditMultiLegGroupSettings1->Enable(current);
  m_mShowRoutingStatus1->Enable(current);
  m_mGoTo->Enable(current);
  m_mGoTo1->Enable(current);
  m_mDelete->Enable(current);
  m_mDelete1->Enable(current);
  m_mCompute->Enable(current);
  m_mCompute1->Enable(current);
  m_mComputeMultiLegSequence1->Enable(current);
  m_mOptimizeMultiLegDeparture1->Enable(current);
  m_panel->m_bCompute->Enable(current);
  m_mSaveAsTrack->Enable(current);
  m_mSaveAsRoute->Enable(current);
  m_mExportRouteAsGPX->Enable(current);
  m_panel->m_bSaveAsTrack->Enable(current);
  m_panel->m_bSaveAsRoute->Enable(current);

  m_mStop->Enable(m_WaitingRouteMaps.size() + m_RunningRouteMaps.size() > 0);
  m_mStop1->Enable(m_WaitingRouteMaps.size() + m_RunningRouteMaps.size() > 0);

  bool cnt = m_panel->m_lWeatherRoutes->GetItemCount() > 0;
  m_mDeleteAll->Enable(cnt);
  m_mComputeAll->Enable(cnt);
  m_mComputeAll1->Enable(cnt);
  m_mSaveAllAsTracks->Enable(cnt);
}

void WeatherRouting::UpdateConfigurations() {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));

    /* get and set configuration to update start/end positions */
    RouteMapConfiguration c = weatherroute->routemapoverlay->GetConfiguration();
    weatherroute->routemapoverlay->SetConfiguration(c);

    weatherroute->Update(this, true);
    UpdateItem(i);
  }
}

void WeatherRouting::UpdateDialogs() {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps();
  if (m_StatisticsDialog.IsShown())
    m_StatisticsDialog.SetRouteMapOverlays(routemapoverlays);

  if (m_ReportDialog.IsShown())
    m_ReportDialog.SetRouteMapOverlays(routemapoverlays);

  if (m_PlotDialog.IsShown())
    m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());
}

bool WeatherRouting::AddConfiguration(RouteMapConfiguration& configuration) {
  if (!configuration.RouteGUID.IsEmpty()) {
    // use stuff from actual route not whatever was saved
    std::unique_ptr<PlugIn_Route> rte =
        GetRoute_Plugin(configuration.RouteGUID);
    if (rte.get() == nullptr) return false;

    PlugIn_Route* proute = rte.get();
    if (!proute) return false;

    PlugIn_Waypoint* pwp;
    wxPlugin_WaypointListNode* pwpnode = proute->pWaypointList->GetFirst();
    if (!pwpnode) return false;

    pwp = pwpnode->GetData();
    AddPosition(pwp->m_lat, pwp->m_lon, pwp->m_MarkName, pwp->m_GUID);
    configuration.Start = pwp->m_MarkName;
    configuration.StartGUID = pwp->m_GUID;
    configuration.StartLat = pwp->m_lat, configuration.StartLon = pwp->m_lon;
    while (pwpnode->GetNext()) {
      pwpnode = pwpnode->GetNext();
    }

    pwp = pwpnode->GetData();
    AddPosition(pwp->m_lat, pwp->m_lon, pwp->m_MarkName, pwp->m_GUID);
    configuration.End = pwp->m_MarkName;
    configuration.EndGUID = pwp->m_GUID;
    configuration.EndLat = pwp->m_lat, configuration.EndLon = pwp->m_lon;
  }
  WeatherRoute* weatherroute = new WeatherRoute;
  RouteMapOverlay* routemapoverlay = weatherroute->routemapoverlay;
  routemapoverlay->SetConfiguration(configuration);
  routemapoverlay->Reset();
  weatherroute->Update(this);

  m_WeatherRoutes.push_back(weatherroute);

  wxListItem item;
  long index = m_panel->m_lWeatherRoutes->InsertItem(
      m_panel->m_lWeatherRoutes->GetItemCount(), item);

  m_panel->m_lWeatherRoutes->SetItemPtrData(index, (wxUIntPtr)weatherroute);
  UpdateItem(index);

  m_mDeleteAll->Enable();
  m_mComputeAll->Enable();
  m_mSaveAllAsTracks->Enable();
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
  return true;
}

void WeatherRouting::UpdateRouteMap(RouteMapOverlay* routemapoverlay) {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (weatherroute->routemapoverlay == routemapoverlay) {
      weatherroute->Update(this);
      UpdateItem(i);
      return;
    }
  }
}

static bool IsDisplayMetricAvailable(RouteMapOverlay* routemapoverlay) {
  return routemapoverlay && routemapoverlay->Finished() &&
         routemapoverlay->ReachedDestination() &&
         routemapoverlay->EndTime().IsValid();
}

static wxString FormatRouteMetric(RouteMapOverlay* routemapoverlay,
                                  RouteMapOverlay::RouteInfoType metric,
                                  const wxString& format, bool available) {
  if (!available) return _("N/A");
  double value = routemapoverlay->RouteInfo(metric);
  if (!std::isfinite(value)) return _("N/A");
  return wxString::Format(format, value);
}

static wxString FormatRouteDistance(RouteMapOverlay* routemapoverlay,
                                    const RouteMapConfiguration& configuration,
                                    bool available) {
  if (!available) return _("N/A");
  double routedDistance = routemapoverlay->RouteInfo(RouteMapOverlay::DISTANCE);
  double directDistance =
      DistGreatCircle_Plugin(configuration.StartLat, configuration.StartLon,
                             configuration.EndLat, configuration.EndLon);
  if (!std::isfinite(routedDistance) || !std::isfinite(directDistance))
    return _("N/A");
  return wxString::Format(_T("%.0f/%.0f"), routedDistance, directDistance);
}

static wxString BuildRouteFailureState(RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return _("Failed");

  wxString explicitReason = routemapoverlay->GetFailureReason();
  if (!explicitReason.IsEmpty()) return explicitReason;

  wxString state;
  bool needsComma = false;
  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  if (configuration.grib_is_data_deficient) {
    state += _("Data Deficient");
    needsComma = true;
  }

  WeatherForecastStatus forecastStatus =
      routemapoverlay->GetWeatherForecastStatus();
  if (forecastStatus != WEATHER_FORECAST_SUCCESS) {
    if (needsComma) state += _T(", ");
    state += RouteMap::GetWeatherForecastStatusMessage(forecastStatus);
    needsComma = true;
  }

  wxString weatherStatus = routemapoverlay->GetWeatherForecastError();
  if (!weatherStatus.IsEmpty()) {
    if (needsComma) state += _T(", ");
    state += _("Grib");
    state += ": ";
    state += weatherStatus;
    needsComma = true;
  }

  PolarSpeedStatus polarStatus = routemapoverlay->GetPolarStatus();
  if (polarStatus != POLAR_SPEED_SUCCESS) {
    if (needsComma) state += _T(", ");
    state += _("Polar");
    state += ": ";
    state += Polar::GetPolarStatusMessage(polarStatus);
    needsComma = true;
  }

  wxString gribError = routemapoverlay->GetGribError();
  if (!gribError.IsEmpty()) {
    if (needsComma) state += _T(", ");
    state += gribError;
    needsComma = true;
  }

  if (routemapoverlay->LandCrossing()) {
    if (needsComma) state += _T(", ");
    state += _("Land");
    state += ": ";
    state += _("Failed");
    needsComma = true;
  }

  if (routemapoverlay->BoundaryCrossing()) {
    if (needsComma) state += _T(", ");
    state += _("Boundary");
    state += ": ";
    state += _("Failed");
    needsComma = true;
  }

  if (!routemapoverlay->ReachedDestination()) {
    if (needsComma) state += _T(", ");
    state += _("Did not reach destination");
    needsComma = true;
  }

  if (routemapoverlay->ReachedDestination() &&
      !routemapoverlay->EndTime().IsValid()) {
    if (needsComma) state += _T(", ");
    state += _("No valid ETA");
    needsComma = true;
  }

  if (state.IsEmpty()) state = _("Failed");
  return state;
}

/* we could speed this up more with another flag for when we need to update
   parameters but not computed route information */
void WeatherRoute::Update(WeatherRouting* wr, bool stateonly) {
  if (!stateonly) {
    RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();

    BoatFilename = configuration.boatFileName;
    // Add handling for Start field based on StartType
    if (configuration.StartType == RouteMapConfiguration::START_FROM_BOAT) {
      Start = _("Boat");
    } else {
      Start = configuration.Start;
    }
    StartType =
        configuration.StartType == RouteMapConfiguration::START_FROM_BOAT
            ? _("From Boat")
        : configuration.StartType == RouteMapConfiguration::START_FROM_WAYPOINT
            ? _("From Waypoint")
            : _("From Position");
    UseCurrentTime = configuration.UseCurrentTime ? _("true") : _("false");
    wxDateTime starttime = configuration.StartTime;
    if (wr->m_SettingsDialog.m_cbUseLocalTime->GetValue())
      starttime = starttime.FromUTC();
    StartTime = starttime.Format(_T("%x %H:%M"));

    End = configuration.End;

    wxDateTime endtime = routemapoverlay->EndTime();
    if (endtime.IsValid()) {
      if (wr->m_SettingsDialog.m_cbUseLocalTime->GetValue())
        endtime = endtime.FromUTC();
      EndTime = endtime.Format(_T("%x %H:%M"));
    } else
      EndTime = _T("N/A");

    // REFACTORING
    // I decided to dedicate a function for displaying the difference
    // between two TimeDate as it is usefull in some other part of the code.
    bool metricsAvailable = IsDisplayMetricAvailable(routemapoverlay);
    Time = metricsAvailable ? calculateTimeDelta(starttime, endtime) : _("N/A");

    Distance = FormatRouteDistance(routemapoverlay, configuration,
                                   metricsAvailable);
    AvgSpeed = FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGSPEED,
                                 _T("%.1f"), metricsAvailable);
    MaxSpeed = FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXSPEED,
                                 _T("%.1f"), metricsAvailable);
    AvgSpeedGround =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGSPEEDGROUND,
                          _T("%.1f"), metricsAvailable);
    MaxSpeedGround =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXSPEEDGROUND,
                          _T("%.1f"), metricsAvailable);
    AvgWind = FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGWIND,
                                _T("%.1f"), metricsAvailable);
    MaxWind = FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXWIND,
                                _T("%.1f"), metricsAvailable);
    MaxWindGust =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXWINDGUST,
                          _T("%.1f"), metricsAvailable);
    AvgCurrent =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGCURRENT,
                          _T("%.1f"), metricsAvailable);
    MaxCurrent =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXCURRENT,
                          _T("%.1f"), metricsAvailable);
    AvgSwell = FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGSWELL,
                                 _T("%.1f"), metricsAvailable);
    MaxSwell = FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXSWELL,
                                 _T("%.1f"), metricsAvailable);
    UpwindPercentage =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::PERCENTAGE_UPWIND,
                          _T("%.1f%%"), metricsAvailable);

    double ps = metricsAvailable
                    ? routemapoverlay->RouteInfo(RouteMapOverlay::PORT_STARBOARD)
                    : NAN;
    PortStarboard =
        metricsAvailable && std::isfinite(ps)
            ? wxString::Format(_T("%.0f/%.0f"), ps, 100 - ps)
            : _("N/A");

    Tacks = FormatRouteMetric(routemapoverlay, RouteMapOverlay::TACKS,
                              _T("%.0f"), metricsAvailable);
    Jibes = FormatRouteMetric(routemapoverlay, RouteMapOverlay::JIBES,
                              _T("%.0f"), metricsAvailable);
    SailPlanChanges =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::SAIL_PLAN_CHANGES,
                          _T("%.0f"), metricsAvailable);

    if (metricsAvailable) {
      int comfort_level = routemapoverlay->RouteInfo(RouteMapOverlay::COMFORT);
      Comfort = RouteMapOverlay::sailingConditionText(comfort_level);
    } else {
      Comfort = _("N/A");
    }
  }

  if (!routemapoverlay->Valid()) {
    State = _("Invalid Start/End");
    wxString error = routemapoverlay->GetError();
    wxString weatherError = routemapoverlay->GetWeatherForecastError();
    if (!error.IsEmpty())
      State += ": " + error;
    else if (!weatherError.IsEmpty())
      State += ": " + weatherError;
  } else if (routemapoverlay->Running())
    State = _("Computing...");
  else {
    if (routemapoverlay->Finished()) {
      if (routemapoverlay->ReachedDestination())
        State = _("Complete");
      else
        State = BuildRouteFailureState(routemapoverlay);
    } else {
      for (std::list<RouteMapOverlay*>::iterator it =
               wr->m_WaitingRouteMaps.begin();
           it != wr->m_WaitingRouteMaps.end(); it++)
        if (*it == routemapoverlay) {
          State = _("Waiting...");
          return;
        }
      State = _("Not Computed");
    }
  }
}

void WeatherRouting::UpdateItem(long index, bool stateonly) {
  WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
      wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
  if (!weatherroute) return;

  if (!stateonly) {
    if (columns[VISIBLE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItemImage(
          index, weatherroute->routemapoverlay->m_bEndRouteVisible ? 0 : -1);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[VISIBLE], 28);
    }

    if (columns[BOAT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(
          index, columns[BOAT],
          wxFileName(weatherroute->BoatFilename).GetName());
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[BOAT], wxLIST_AUTOSIZE);
    }

    if (columns[START_TYPE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[START_TYPE],
                                         weatherroute->StartType);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[START_TYPE],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[START] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[START],
                                         weatherroute->Start);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[START],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[STARTTIME] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[STARTTIME],
                                         weatherroute->StartTime);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[STARTTIME],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[END] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[END],
                                         weatherroute->End);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[END], wxLIST_AUTOSIZE);
    }

    if (columns[ENDTIME] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[ENDTIME],
                                         weatherroute->EndTime);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[ENDTIME],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[TIME] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[TIME],
                                         weatherroute->Time);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[TIME], wxLIST_AUTOSIZE);
    }

    if (columns[DISTANCE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[DISTANCE],
                                         weatherroute->Distance);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[DISTANCE],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGSPEED] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGSPEED],
                                         weatherroute->AvgSpeed);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGSPEED],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXSPEED] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXSPEED],
                                         weatherroute->MaxSpeed);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXSPEED],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGSPEEDGROUND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGSPEEDGROUND],
                                         weatherroute->AvgSpeedGround);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGSPEEDGROUND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXSPEEDGROUND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXSPEEDGROUND],
                                         weatherroute->MaxSpeedGround);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXSPEEDGROUND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGWIND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGWIND],
                                         weatherroute->AvgWind);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGWIND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXWIND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXWIND],
                                         weatherroute->MaxWind);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXWIND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXWINDGUST] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXWINDGUST],
                                         weatherroute->MaxWindGust);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXWINDGUST],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGCURRENT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGCURRENT],
                                         weatherroute->AvgCurrent);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGCURRENT],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXCURRENT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXCURRENT],
                                         weatherroute->MaxCurrent);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXCURRENT],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGSWELL] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGSWELL],
                                         weatherroute->AvgSwell);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGSWELL],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXSWELL] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXSWELL],
                                         weatherroute->MaxSwell);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXSWELL],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[UPWIND_PERCENTAGE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[UPWIND_PERCENTAGE],
                                         weatherroute->UpwindPercentage);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[UPWIND_PERCENTAGE],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[PORT_STARBOARD] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[PORT_STARBOARD],
                                         weatherroute->PortStarboard);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[PORT_STARBOARD],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[TACKS] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[TACKS],
                                         weatherroute->Tacks);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[TACKS],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[JIBES] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[JIBES],
                                         weatherroute->Jibes);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[JIBES],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[SAIL_PLAN_CHANGES] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[SAIL_PLAN_CHANGES],
                                         weatherroute->SailPlanChanges);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[SAIL_PLAN_CHANGES],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[COMFORT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[COMFORT],
                                         weatherroute->Comfort);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[COMFORT],
                                                wxLIST_AUTOSIZE);
    }
  }

  if (columns[STATE] >= 0) {
    m_panel->m_lWeatherRoutes->SetItem(index, columns[STATE],
                                       weatherroute->State);
    m_panel->m_lWeatherRoutes->SetColumnWidth(columns[STATE], wxLIST_AUTOSIZE);
  }
}

// The configuration changed, so stop computation and update the display in the
// list
void WeatherRouting::SetConfigurationRoute(WeatherRoute* weatherroute) {
  if (m_bSkipUpdateCurrentItems) return;

  RouteMapOverlay* rmo = weatherroute->routemapoverlay;
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end(); it++)
    if (*it == rmo && rmo->Running()) rmo->DeleteThread();

  if (m_ApplyingMultiLegGroupSettings && rmo &&
      !m_MultiLegSettingsGroupId.IsEmpty()) {
    RouteMapConfiguration configuration = rmo->GetConfiguration();
    if (configuration.IsMultiLegGenerated &&
        configuration.MultiLegGroupId == m_MultiLegSettingsGroupId) {
      PreserveMultiLegLegFields(rmo, configuration);
      rmo->SetConfiguration(configuration);
      if (!RouteMapIsWaitingOrRunning(rmo)) rmo->Reset();
    }
  }

  weatherroute->Update(this);

  for (long index = 0; index < m_panel->m_lWeatherRoutes->GetItemCount();
       index++) {
    WeatherRoute* wr = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
    if (weatherroute == wr) {
      UpdateItem(index);
      break;
    }
  }
}

void WeatherRouting::UpdateBoatFilename(wxString boatFileName) {
  for (long index = 0; index < m_panel->m_lWeatherRoutes->GetItemCount();
       index++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));

    RouteMapConfiguration c = weatherroute->routemapoverlay->GetConfiguration();
    if (c.boatFileName == boatFileName) {
      RouteMapOverlay* rmo = weatherroute->routemapoverlay;
      rmo->ResetFinished();
      SetConfigurationRoute(weatherroute);
    }
  }
}

void WeatherRouting::UpdateCurrentConfigurations() {
  long index = -1;
  for (;;) {
    index = m_panel->m_lWeatherRoutes->GetNextItem(index, wxLIST_NEXT_ALL,
                                                   wxLIST_STATE_SELECTED);
    if (index == -1) break;
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
    SetConfigurationRoute(weatherroute);
  }
}

void WeatherRouting::UpdateStates() {
  for (std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
       it != m_WeatherRoutes.end(); it++)
    (*it)->Update(this, true);
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++)
    UpdateItem(i, true);
}

std::list<RouteMapOverlay*> WeatherRouting::CurrentRouteMaps(
    bool messagedialog) {
  std::list<RouteMapOverlay*> routemapoverlays;
  long index = -1;
  if (m_panel)
    for (;;) {
      index = m_panel->m_lWeatherRoutes->GetNextItem(index, wxLIST_NEXT_ALL,
                                                     wxLIST_STATE_SELECTED);
      if (index == -1) break;
      routemapoverlays.push_back(
          reinterpret_cast<WeatherRoute*>(
              wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)))
              ->routemapoverlay);
    }

  if (messagedialog && routemapoverlays.empty()) {
    wxMessageDialog mdlg(this, _("No Weather Route selected"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
  }

  return routemapoverlays;
}

RouteMapOverlay* WeatherRouting::FirstCurrentRouteMap() {
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  return currentroutemaps.empty() ? NULL : currentroutemaps.front();
}

void WeatherRouting::RebuildList() {
  m_panel->m_lWeatherRoutes->DeleteAllItems();
  for (std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
       it != m_WeatherRoutes.end(); it++) {
    if (!(*it)->Filtered) {
      wxListItem item;
      item.SetId(m_panel->m_lWeatherRoutes->GetItemCount());
      item.SetData(*it);
      UpdateItem(m_panel->m_lWeatherRoutes->InsertItem(item));
    }
  }
}

void WeatherRouting::SaveAsTrack(RouteMapOverlay& routemapoverlay) {
  std::list<PlotData> plotdata = routemapoverlay.GetPlotData(false);

  if (plotdata.empty()) {
    wxMessageDialog mdlg(this, _("Empty routing, nothing to save\n"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }

  PlugIn_Track* newPath = new PlugIn_Track;
  wxDateTime display_time = routemapoverlay.StartTime();
  if (m_SettingsDialog.m_cbUseLocalTime->GetValue())
    display_time = display_time.FromUTC();

  newPath->m_NameString =
      _("Weather Route ") + " (" + display_time.Format(_T("%x %H:%M")) + ")";

  // XXX double check time is really end time, not start time off by one.
  RouteMapConfiguration c = routemapoverlay.GetConfiguration();
  newPath->m_StartString = c.Start;
  newPath->m_EndString = c.End;

  for (auto const& it : plotdata) {
    PlugIn_Waypoint* newPoint =
        new PlugIn_Waypoint(it.lat, heading_resolve(it.lon), _T("circle"),
                            _("Weather Route Point"));

    newPoint->m_CreateTime = it.time;
    newPath->pWaypointList->Append(newPoint);
  }

  // last point, missing if config didn't succeed
  Position* p = routemapoverlay.GetDestination();
  if (p) {
    PlugIn_Waypoint* newPoint = new PlugIn_Waypoint(
        p->lat, p->lon, _T("circle"), _("Weather Route Destination"));
    newPoint->m_CreateTime = routemapoverlay.EndTime();
    newPath->pWaypointList->Append(newPoint);
  }

  AddPlugInTrack(newPath);
  // not done PlugIn_Track DTOR
  newPath->pWaypointList->DeleteContents(true);
  newPath->pWaypointList->Clear();

  delete newPath;

  GetParent()->Refresh();

  wxMessageDialog mdlg(this,
                       _("Routing has been saved as a track in the 'Route and "
                         "Mark' Manager\n"),
                       _("Weather Routing"), wxOK);
  mdlg.ShowModal();
}

void WeatherRouting::SaveAsRoute(RouteMapOverlay& routemapoverlay) {
  std::list<PlotData> plotdata = routemapoverlay.GetPlotData(false);

  if (plotdata.empty()) {
    wxMessageDialog mdlg(this, _("Empty routing, nothing to save\n"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }

  PlugIn_Route_Ex* newRoute = new PlugIn_Route_Ex();
  wxDateTime display_time = routemapoverlay.StartTime();
  if (m_SettingsDialog.m_cbUseLocalTime->GetValue())
    display_time = display_time.FromUTC();

  newRoute->m_NameString =
      _("Weather Route ") + " (" + display_time.Format(_T("%x %H:%M")) + ")";

  RouteMapConfiguration c = routemapoverlay.GetConfiguration();
  newRoute->m_StartString = c.Start;
  newRoute->m_EndString = c.End;
  newRoute->m_isVisible = true;

  for (auto const& it : plotdata) {
    PlugIn_Waypoint_Ex* newPoint =
        new PlugIn_Waypoint_Ex(it.lat, heading_resolve(it.lon), _T("circle"),
                               _("Weather Route Point"));
    // newPoint->m_PlannedSpeed = it.sog;
    newPoint->m_CreateTime = it.time;
    newRoute->pWaypointList->Append(newPoint);
  }

  // last point, missing if config didn't succeed
  Position* p = routemapoverlay.GetDestination();
  if (p) {
    PlugIn_Waypoint_Ex* newPoint = new PlugIn_Waypoint_Ex(
        p->lat, p->lon, _T("circle"), _("Weather Route Destination"));
    newPoint->m_CreateTime = routemapoverlay.EndTime();
    newRoute->pWaypointList->Append(newPoint);
  }

  AddPlugInRouteEx(newRoute);
  // Clean up waypoint list (ownership transferred to OpenCPN)
  newRoute->pWaypointList->DeleteContents(true);
  newRoute->pWaypointList->Clear();

  delete newRoute;

  GetParent()->Refresh();

  wxMessageDialog mdlg(this,
                       _("Routing has been saved as a route in the 'Route and "
                         "Mark' Manager\n"),
                       _("Weather Routing"), wxOK);
  mdlg.ShowModal();
}

void WeatherRouting::ExportRoute(RouteMapOverlay& routemapoverlay) {
  std::list<PlotData> plotdata = routemapoverlay.GetPlotData(false);

  if (plotdata.empty()) {
    wxMessageDialog mdlg(this, _("Empty Routing, nothing to export\n"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }

  RouteMapConfiguration c = routemapoverlay.GetConfiguration();

  SimpleRoute new_route;
  new_route.m_GUID = GetNewGUID();

  wxDateTime display_time = routemapoverlay.StartTime();
  if (m_SettingsDialog.m_cbUseLocalTime->GetValue())
    display_time = display_time.FromUTC();

  new_route.m_RouteNameString =
      "WXRoute_" + display_time.Format(_T("%m-%d-%y_%H-%M"));
  new_route.m_RouteNameString += "_" + c.Start + "_" + c.End;

  new_route.m_RouteStartString = c.Start;
  new_route.m_RouteEndString = c.End;
  new_route.m_PlannedDeparture = routemapoverlay.StartTime();

  std::vector<double> lat;
  std::vector<double> lon;
  std::vector<wxDateTime> time;
  std::vector<double> vmga;
  for (auto const& it0 : plotdata) {
    lat.push_back(it0.lat);
    lon.push_back(heading_resolve(it0.lon));
    time.push_back(it0.time);
    vmga.push_back(-1.);
  }

  unsigned int ip = 0;
  for (auto const& it1 : plotdata) {
    // calculate leg parameters, mainly VMG
    double vmg = -1.;
    if (ip < time.size() - 1) {
      wxTimeSpan delta_time = time[ip + 1] - time[ip];
      double secs = delta_time.GetSeconds().ToDouble();
      double distance =
          DistGreatCircle_Plugin(lat[ip + 1], lon[ip + 1], lat[ip], lon[ip]);
      vmg = (distance / secs) * 3600;
    }
    vmga[ip + 1] = vmg;  // assign VMG to last (or second) point in leg
    ip++;
  }

  unsigned int ip1 = 0;
  // Use some part of new route GUID to uniquely name route points
  wxString route_name_suffix = new_route.m_GUID.AfterLast('-').Truncate(4);

  for (auto const& it : plotdata) {
    wxString wp_name("RP-");
    wp_name += route_name_suffix;
    wxString np;
    np.Printf("-%d", ip1);
    wp_name += np;

    SimpleRoutePoint* newPoint = new SimpleRoutePoint(
        it.lat, heading_resolve(it.lon), _T("circle"), wp_name, GetNewGUID());

    if (vmga[ip1] >= 0.) newPoint->m_seg_vmg = vmga[ip1];

    newPoint->m_CreateTime = it.time;
    if (ip1 > 0) newPoint->etd = time[ip1 - 1];

    new_route.AddPoint(newPoint);
    ip1++;
  }

  // last point, missing if config didn't succeed
  Position* p = routemapoverlay.GetDestination();
  if (p) {
    SimpleRoutePoint* newPoint = new SimpleRoutePoint(
        p->lat, p->lon, _T("circle"), _("Weather Route Destination"));
    newPoint->m_CreateTime = routemapoverlay.EndTime();
    new_route.AddPoint(newPoint);
  }

  SimpleNavObjectXML* navobj = new SimpleNavObjectXML;
  navobj->CreateNavObjGPXRoute(new_route);

  wxString export_path_base = weather_routing_pi::StandardPath() +
                              _T("PlannedRoutes") +
                              wxFileName::GetPathSeparator();
  if (!wxDir::Exists(export_path_base)) wxDir::Make(export_path_base);

  // Handle duplicate file names by adding "(n)" as needed
  export_path_base += new_route.m_RouteNameString;
  wxString export_path = export_path_base + ".gpx";
  if (wxFileName::Exists(export_path)) {
    int iv = 1;
    bool bok = false;
    wxString tname, vadd;
    while (!bok && iv < 10) {
      vadd.Printf("(%d)", iv);
      wxString tname = export_path_base + vadd + ".gpx";
      if (wxFileName::Exists(tname)) {
        iv++;
      } else
        bok = true;
    }
    if (bok) export_path_base += vadd;
  }

  export_path = export_path_base + ".gpx";

  bool bsave_ok = navobj->save_file(export_path.ToStdString().c_str());
  if (bsave_ok) {
    wxMessageBox(_("GPX Route file created") + ".\n\n" + export_path + "\n",
                 _("OpenCPN Weather Routing Plugin"), wxOK);
  } else {
    wxMessageBox(
        _("GPX Route file export failed") + ".\n\n" + export_path + "\n",
        _("OpenCPN Weather Routing Plugin"), wxICON_ERROR | wxOK);
  }

  delete navobj;

  GetParent()->Refresh();
}

void WeatherRouting::Start(RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  bool boatHasMoved = false;
  if (routemapoverlay->Finished() &&
      configuration.StartType == RouteMapConfiguration::START_FROM_BOAT) {
    // Check if the boat has moved significantly since the last calculation.
    double distance = DistGreatCircle_Plugin(
        configuration.StartLat, configuration.StartLon,
        m_weather_routing_pi.m_boat_lat, m_weather_routing_pi.m_boat_lon);
    // Threshold for significant movement is somewhat arbitrarily set to 20
    // meters.
    boatHasMoved = (distance * 1852.0) > 20.0;
  }

  // Skip recalculation if:
  // 1. The route has completed, or
  // 2. Route is from boat and boat has moved, or
  // 3. Configuration specifies to use current start time.
  if (routemapoverlay->Finished() &&
      routemapoverlay->GetWeatherForecastStatus() == WEATHER_FORECAST_SUCCESS &&
      !boatHasMoved && !configuration.UseCurrentTime) {
    return;
  }

  bool configUpdated = false;
  // If starting from boat, update the boat position
  if (configuration.StartType == RouteMapConfiguration::START_FROM_BOAT) {
    // Use the current boat position from the plugin
    configuration.StartLat = m_weather_routing_pi.m_boat_lat;
    configuration.StartLon = m_weather_routing_pi.m_boat_lon;
    // Set "Boat" as the starting point name for display purposes
    configuration.Start = _("Boat");
    // Clear any StartGUID since we're using the boat position, not a waypoint
    configuration.StartGUID = wxEmptyString;
    configUpdated = true;
  }
  if (configuration.UseCurrentTime) {
    // Use the current time
    configuration.StartTime = wxDateTime::Now().ToUTC();
    configUpdated = true;
  }
  if (configUpdated) {
    // Call Update() to recalculate the bearing and other parameters
    configuration.Update();
    routemapoverlay->SetConfiguration(configuration);
  }
  // Ensure we have valid start coordinates
  if (std::isnan(configuration.StartLat) ||
      std::isnan(configuration.StartLon)) {
    routemapoverlay->SetError(_("Invalid start position"));
    return;
  }

  if (configuration.DeltaTime <= 0) {
    routemapoverlay->SetError(_("Zero Time Step"));
    return;
  }
  if (configuration.DetectBoundary) {
    if (m_weather_routing_pi.InBoundary(configuration.EndLat,
                                        configuration.EndLon) ||
        m_weather_routing_pi.InBoundary(configuration.StartLat,
                                        configuration.StartLon)) {
      routemapoverlay->SetError(_("inside exclusion boundary"));
      return;
    }
  }

  if (fabs(configuration.StartLat) > configuration.MaxLatitude ||
      fabs(configuration.EndLat) > configuration.MaxLatitude) {
    routemapoverlay->SetError(_("lies outside of Max Latitude constraint"));
    return;
  }

  /* initialize crossing land routine from main thread as it is
     not re-entrant, and cannot be done by worker-threads later */
  if (configuration.DetectLand) {
    bool use_experimental_chart_safety = false;
    bool enforce_experimental_chart_safety = false;
    ReadExperimentalChartSafetySettings(use_experimental_chart_safety,
                                        enforce_experimental_chart_safety);
    ConstraintChecker::ResetSegmentSafetyDiagnostics(
        use_experimental_chart_safety, enforce_experimental_chart_safety);
    ConstraintChecker::SetSegmentSafetyDiagnosticContext(wxString::Format(
        _("route=\"%s to %s\" group=%s candidate_offset=%d leg=%d/%d"),
        configuration.Start, configuration.End, configuration.MultiLegGroupId,
        configuration.DepartureTimeOptimizationOffsetMinutes,
        configuration.MultiLegLegIndex, configuration.MultiLegLegCount));
    PlugIn_GSHHS_CrossesLand(0, 0, 0, 0);
    PrewarmExperimentalChartSafetyForConfiguration(configuration,
                                                  _("route start"));
    if (!s_loggedDetectLandGshhsWarning) {
      wxLogMessage(
          use_experimental_chart_safety
              ? (enforce_experimental_chart_safety
                     ? "WeatherRouting Detect Land: initializing "
                       "experimental OpenCPN chart-backed segment safety "
                       "checks with enforcement enabled. Propagation may "
                       "fall back if the performance guard triggers; "
                       "final-route chart validation remains active."
                     : "WeatherRouting Detect Land: initializing "
                       "experimental OpenCPN chart-backed segment safety "
                       "diagnostics. GSHHS remains the enforcement path.")
              : "WeatherRouting Detect Land: initializing GSHHS shoreline "
                "checks.");
      s_loggedDetectLandGshhsWarning = true;
    }
  }

  /* same with grib */
  if (!configuration.RouteGUID.IsEmpty() && configuration.UseGrib)
    SendPluginMessage(wxS("GRIB_VALUES_REQUEST"), _T(""));

  if (configuration.ClimatologyType != RouteMapConfiguration::DISABLED) {
    /* query climatology to load it from main thread */
    double dir, speed;
    if (RouteMap::ClimatologyData)
      RouteMap::ClimatologyData(0, wxDateTime::Now(), 0, 0, dir, speed);
  }

  // already running?
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end(); it++)
    if (*it == routemapoverlay) return;

  if (!m_bRunning) m_StatisticsDialog.SetRunTime(m_RunTime = wxTimeSpan(0));

  // already waiting?
  for (std::list<RouteMapOverlay*>::iterator it = m_WaitingRouteMaps.begin();
       it != m_WaitingRouteMaps.end(); it++) {
    if (*it == routemapoverlay) return;
  }
  routemapoverlay->Reset();
  m_RoutesToRun++;
  m_WaitingRouteMaps.push_back(routemapoverlay);
  SetEnableConfigurationMenu();
  UpdateRouteMap(routemapoverlay);
}

void WeatherRouting::StartAll() {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    Start(weatherroute->routemapoverlay);
  }
}

void WeatherRouting::Stop(RouteMapOverlay* routemapoverlay) {
  routemapoverlay->Stop();
  // Wait for threads to finish
  while (routemapoverlay->Running()) wxThread::Sleep(100);
  routemapoverlay->ResetFinished();
  routemapoverlay->DeleteThread();
}

void WeatherRouting::StopAll() {
  CancelMultiLegSequence();

  /* stop all the threads at once, rather than waiting for each one before
     telling the next to stop */
  for (auto it : m_RunningRouteMaps) it->Stop();

  wxProgressDialog* progressdialog = NULL;

  int c = 0;
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end(); it++) {
    // Wait for threads to finish
    while ((*it)->Running()) wxThread::Sleep(100);

    (*it)->ResetFinished();
    (*it)->DeleteThread();

    if (progressdialog) progressdialog->Update(c++);
  }

  delete progressdialog;

  m_RunningRouteMaps.clear();
  m_WaitingRouteMaps.clear();

  UpdateStates();

  m_RoutesToRun = 0;
  m_panel->m_gProgress->SetValue(0);
  m_bRunning = false;

  SetEnableConfigurationMenu();
  if (m_StartTime.IsValid())
    m_StatisticsDialog.SetRunTime(m_RunTime += wxDateTime::Now() - m_StartTime);
}

void WeatherRouting::Reset() {
  if (m_bRunning) StopAll();

  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    weatherroute->routemapoverlay->Reset();
  }
  m_positionOnRoute = nullptr;
  UpdateDialogs();

  GetParent()->Refresh();
}

void WeatherRouting::DeleteRouteMaps(
    std::list<RouteMapOverlay*> routemapoverlays) {
  bool current = false;
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++) {
    std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
    for (std::list<RouteMapOverlay*>::iterator cit = currentroutemaps.begin();
         cit != currentroutemaps.end(); cit++)
      if (*it == *cit) {
        current = true;
        break;
      }

    for (std::list<RouteMapOverlay*>::iterator wit = m_WaitingRouteMaps.begin();
         wit != m_WaitingRouteMaps.end(); wit++)
      if (*it == *wit) {
        m_WaitingRouteMaps.erase(wit);
        break;
      }

    for (std::list<RouteMapOverlay*>::iterator rit = m_RunningRouteMaps.begin();
         rit != m_RunningRouteMaps.end(); rit++)
      if (*it == *rit) {
        m_RunningRouteMaps.erase(rit);
        break;
      }

    for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
      if (weatherroute->routemapoverlay == *it) {
        m_panel->m_lWeatherRoutes->DeleteItem(i);
        break;
      }
    }

    for (std::list<WeatherRoute*>::iterator writ = m_WeatherRoutes.begin();
         writ != m_WeatherRoutes.end(); writ++)
      if ((*writ)->routemapoverlay == *it) {
        delete *writ;
        m_WeatherRoutes.erase(writ);
        break;
      }
  }

  m_ReportDialog.m_bReportStale = true;

  SetEnableConfigurationMenu();

  if (current) UpdateDialogs();
}

RouteMapConfiguration WeatherRouting::DefaultConfiguration() {
  RouteMapConfiguration configuration;

  if (RouteMap::Positions.size() >= 1) {
    RouteMapPosition& p = *RouteMap::Positions.begin();
    configuration.Start = p.Name;
    configuration.StartLat = p.lat, configuration.StartLon = p.lon;
  } else
    configuration.StartLat = 0, configuration.StartLon = 0;

  configuration.StartTime = wxDateTime::Now();
  configuration.DeltaTime = 3600;

  if (RouteMap::Positions.size() >= 2) {
    RouteMapPosition& p = *(++RouteMap::Positions.begin());
    configuration.End = p.Name;
    configuration.EndLat = p.lat, configuration.EndLon = p.lon;
  } else
    configuration.EndLat = 0, configuration.EndLon = 0;

  configuration.boatFileName = weather_routing_pi::StandardPath() +
                               _T("boats") + wxFileName::GetPathSeparator() +
                               _T("Boat.xml");

  configuration.Integrator = RouteMapConfiguration::NEWTON;

  configuration.MaxDivertedCourse = 90;
  configuration.MaxCourseAngle = 180;
  configuration.MaxSearchAngle = 120;
  configuration.MaxTrueWindKnots = 50;      // Safety margin for wind speed
  configuration.MaxApparentWindKnots = 50;  // Safety margin for wind speed

  configuration.MaxSwellMeters = 20.;
  configuration.MaxLatitude = 90;
  configuration.TackingTime = 0;
  configuration.JibingTime = 0;
  configuration.SailPlanChangeTime = 0;
  configuration.WindVSCurrent = 0;

  configuration.AvoidCycloneTracks = false;
  configuration.CycloneMonths = 1;
  configuration.CycloneDays = 0;

  configuration.UseGrib = true;
  configuration.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
  configuration.AllowDataDeficient = false;
  configuration.WindStrength = 1;
  configuration.DetectLand = true;
  configuration.SafetyMarginLand = 0.;
  configuration.DetectBoundary = false;
  configuration.Currents = false;
  configuration.OptimizeTacking = false;
  configuration.InvertedRegions = false;
  configuration.Anchoring = false;

  configuration.FromDegree = 0;
  configuration.ToDegree = 180;
  configuration.ByDegrees = 5;

  return configuration;
}
