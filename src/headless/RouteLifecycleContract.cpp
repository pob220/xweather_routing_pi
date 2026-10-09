// Opt-in integration checks run inside a disposable genuine OpenCPN host.
// They exercise the real list event, renderer, reset and computation queues.
#include <wx/wx.h>
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "RoutingTablePanel.h"
#include "pidc.h"

#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/utils.h>

bool WeatherRouting::RunRouteLifecycleContract(RouteMapOverlay* route) {
  bool passed = true;
  const auto check = [&](bool condition, const char* name) {
    wxLogMessage("WR_ROUTE_LIFECYCLE check=%s passed=%d", name, condition);
    passed &= condition;
  };
  if (!route) return false;
  check(route->Finished() && route->ReachedDestination(), "completed_fixture");
  if (!passed) return false;
  const auto original = route->GetConfiguration();
  Show(true);
  SetSize(wxSize(1200, 650));
  Layout();
  wxYield();
  auto* list = m_panel->m_lWeatherRoutes;
  long selectedRow = -1;
  for (long row = 0; row < list->GetItemCount(); ++row) {
    auto* item =
        reinterpret_cast<WeatherRoute*>(wxUIntToPtr(list->GetItemData(row)));
    const bool selected = item->routemapoverlay == route;
    list->SetItemState(row, selected ? wxLIST_STATE_SELECTED : 0,
                       wxLIST_STATE_SELECTED);
    if (selected) selectedRow = row;
  }
  if (selectedRow < 0) return false;
  list->EnsureVisible(selectedRow);
  OnWeatherRouteSelected();

  // Measure actual pixels, so a successful flag change cannot mask a
  // selection-driven redraw which still paints the hidden route.
  PlugIn_ViewPort vp{};
  vp.clat = (original.StartLat + original.EndLat) / 2;
  vp.clon = (original.StartLon + original.EndLon) / 2;
  vp.view_scale_ppm = 0.01;
  vp.chart_scale = 1000000;
  vp.pix_width = 640;
  vp.pix_height = 480;
  vp.rv_rect = wxRect(0, 0, 640, 480);
  vp.m_projection_type = 1;  // Mercator
  vp.lat_min = vp.clat - 1;
  vp.lat_max = vp.clat + 1;
  vp.lon_min = vp.clon - 1;
  vp.lon_max = vp.clon + 1;
  vp.bValid = true;
  const auto paintedPixels = [&]() {
    wxBitmap bitmap(640, 480, 24);
    wxMemoryDC memory(bitmap);
    memory.SetBackground(*wxWHITE_BRUSH);
    memory.Clear();
    piDC dc(memory);
    Render(dc, vp);
    memory.SelectObject(wxNullBitmap);
    const wxImage image = bitmap.ConvertToImage();
    const unsigned char* data = image.GetData();
    unsigned int painted = 0;
    for (int i = 0; i < 640 * 480; ++i)
      if (data[3 * i] != 255 || data[3 * i + 1] != 255 ||
          data[3 * i + 2] != 255)
        ++painted;
    return painted;
  };
  SetRouteVisibility(route, true);
  check(paintedPixels() > 0, "visible_selected_route_paints");
  wxRect bounds;
  list->GetItemRect(selectedRow, bounds);
  wxMouseEvent click(wxEVT_LEFT_DOWN);
  click.SetPosition(wxPoint(14, bounds.y + bounds.height / 2));
  // Generic GTK hit testing distinguishes the actual icon/text from blank
  // cell padding. Find a point on the first-column item, as a user would.
  for (int y = 0; y < list->GetClientSize().y; ++y) {
    bool found = false;
    for (int x = 0; x < list->GetColumnWidth(columns[VISIBLE]); ++x) {
      int flags = 0;
      long column = -1;
      if (list->HitTest(wxPoint(x, y), flags, &column) == selectedRow &&
          column == columns[VISIBLE]) {
        click.SetPosition(wxPoint(x, y));
        found = true;
        break;
      }
    }
    if (found) break;
  }
  int hitFlags = 0;
  wxLogMessage(
      "WR_ROUTE_LIFECYCLE eye_geometry row=%ld bounds=%d,%d,%d,%d hit=%ld "
      "visible_column=%d width=%d shown=%d",
      selectedRow, bounds.x, bounds.y, bounds.width, bounds.height,
      list->HitTest(click.GetPosition(), hitFlags), columns[VISIBLE],
      columns[VISIBLE] >= 0 ? list->GetColumnWidth(columns[VISIBLE]) : -1,
      list->IsShownOnScreen());
  list->GetEventHandler()->ProcessEvent(click);
  check(!route->m_bEndRouteVisible, "eye_event_hides_selected_route");
  check(FirstCurrentRouteMap() == route, "eye_keeps_selection_for_inspection");
  check(paintedPixels() == 0, "hidden_selected_route_paints_nothing");
  list->GetEventHandler()->ProcessEvent(click);
  check(route->m_bEndRouteVisible && paintedPixels() > 0,
        "eye_event_reversibly_restores_route");
  SetRouteVisibility(route, false);

  auto otherConfiguration = original;
  otherConfiguration.EndLon += 0.5;
  wxString pass;
  wxGetEnv("WR_HEADLESS_LIFECYCLE_CONTRACT", &pass);
  otherConfiguration.End = "Lifecycle second route " + pass;
  AddPosition(otherConfiguration.EndLat, otherConfiguration.EndLon,
              otherConfiguration.End);
  if (!AddConfiguration(otherConfiguration)) return false;
  RouteMapOverlay* other = m_WeatherRoutes.back()->routemapoverlay;
  Start(other);
  check(RouteComputationActive(other), "second_route_queued");
  check(!CanClearComputedResults({other}), "queued_results_cannot_be_cleared");
  m_PreparingChartSafetyRoutes.insert(route);
  check(!CanClearComputedResults({route}),
        "preparing_results_cannot_be_cleared");
  m_PreparingChartSafetyRoutes.erase(route);
  check(CanClearComputedResults({route}),
        "finished_route_clearable_during_other_job");

  if (m_RoutingTablePanel) m_RoutingTablePanel->SetRouteMap(route);
  wxCommandEvent clear(wxEVT_MENU, m_mClearResults->GetId());
  m_mConfiguration->ProcessEvent(clear);
  check(!route->HasComputedResults() && !route->Finished() &&
            !route->ReachedDestination() &&
            route->GetDestination() == nullptr && !route->EndTime().IsValid() &&
            route->GetPlotData().empty() && route->RetainedCandidates().empty(),
        "all_computed_result_data_released");
  check(!route->m_bEndRouteVisible, "clearing_preserves_visibility_choice");
  check(RouteComputationActive(other), "clearing_does_not_cancel_other_job");
  const auto after = route->GetConfiguration();
  check(after.Start == original.Start && after.End == original.End &&
            after.StartTime == original.StartTime &&
            after.StartLat == original.StartLat &&
            after.EndLon == original.EndLon &&
            after.boatFileName == original.boatFileName &&
            after.DeltaTime == original.DeltaTime &&
            after.EngineSettings.EngineId() ==
                original.EngineSettings.EngineId() &&
            after.DetectLand == original.DetectLand &&
            after.UseGrib == original.UseGrib,
        "route_settings_preserved");
  DeleteRouteMaps({other});
  --m_RoutesToRun;
  if (m_RoutingTablePanel)
    check(m_RoutingTablePanel->GetRouteMap() != route ||
              route->GetPlotData().empty(),
          "result_table_has_no_stale_data");
  check(paintedPixels() == 0, "cleared_route_paints_nothing");

  // Failed preflight and stopped calculations must also be clearable.
  route->SetError("Lifecycle synthetic preflight failure");
  check(CanClearComputedResults({route}) && ClearComputedResults({route}) &&
            !route->HasComputedResults(),
        "failed_preflight_results_clearable");
  wxLogMessage("WR_ROUTE_LIFECYCLE result=%s", passed ? "passed" : "failed");
  return passed;
}
