// SPDX-License-Identifier: GPL-3.0-or-later
// Private Linux integration probe, linked only by test-settings-reset-linux.py.
#include <wx/wx.h>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "ResultsWorkspace.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <wx/tooltip.h>
#include <fstream>
#include <cstdlib>
class ResetDriver : public wxTimer {
public:
  WeatherRouting* routing{};
  RouteMapOverlay* route{};
  int ticks = 0;
  bool applied = false, started = false, cancel = false, startInPreview = false;
  void Begin(bool start, bool reject = false) {
    ticks = 0;
    applied = false;
    started = false;
    cancel = reject;
    startInPreview = start;
    Start(20);
  }
  void Notify() override {
    ++ticks;
    GtkWidget* dialog = nullptr;
    GList* windows = gtk_window_list_toplevels();
    for (GList* node = windows; node; node = node->next) {
      auto* w = GTK_WINDOW(node->data);
      const char* t = gtk_window_get_title(w);
      if (t && std::string(t) == "Reset engine to preset" && GTK_IS_DIALOG(w))
        dialog = GTK_WIDGET(w);
    }
    g_list_free(windows);
    if (dialog && startInPreview && !started) {
      started = true;
      wxCommandEvent event;
      routing->OnCompute(event);
      return;
    }
    if (dialog && (!startInPreview || route->Running())) {
      wxLogMessage("QA_RESET_RESPONSE cancel=%d running=%d active=%d dt=%.0f",
                   cancel, route->Running(),
                   routing->RouteComputationActive(route),
                   route->GetConfiguration().DeltaTime);
      applied = !cancel;
      Stop();
      gtk_dialog_response(GTK_DIALOG(dialog),
                          cancel ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK);
      return;
    }
    if (ticks > 200) {
      Stop();
      if (dialog) gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    }
  }
};
class Probe : public wxEvtHandler {
  wxTimer timer{this};
  int ticks = 0, phase = 0;
  WeatherRouting* routing = nullptr;
  ResetDriver reset;
  bool passed = true;
  RouteMapConfiguration saved;

public:
  Probe() {
    Bind(wxEVT_TIMER, &Probe::Tick, this);
    timer.Start(500);
  }
  void Reset(bool start, bool cancel = false) {
    auto& setup = routing->m_ConfigurationDialog;
    setup.SetConfigurations(
        {routing->FirstCurrentRouteMap()->GetConfiguration()});
    setup.m_cEnginePreset->SetSelection(0);
    timer.Stop();
    reset.routing = routing;
    reset.route = routing->FirstCurrentRouteMap();
    reset.Begin(start, cancel);
    wxCommandEvent event;
    wxStopWatch watch;
    setup.OnResetAdvanced(event);
    wxLogMessage("QA_RESET_RETURN phase=%d elapsed_ms=%ld dt=%.0f notice=%s",
                 phase, watch.Time(), reset.route->GetConfiguration().DeltaTime,
                 setup.m_validationStatus->GetLabel());
    passed &= watch.Time() < 2000;
    timer.Start(500);
  }
  void Tick(wxTimerEvent&) {
    if (++ticks > 170) {
      Finish(false);
      return;
    }
    if (!routing) {
      for (auto* w : wxTopLevelWindows)
        if (auto* f = wxDynamicCast(w, wxFrame);
            f && f->GetTitle().Contains("WeatherRouting")) {
          routing = static_cast<WeatherRouting*>(f);
          break;
        }
      if (!routing || routing->m_WeatherRoutes.empty()) return;
    }
    auto* route = routing->FirstCurrentRouteMap();
    auto& setup = routing->m_ConfigurationDialog;
    if (phase == 0) {
      saved = route->GetConfiguration();
      auto c = saved;
      c.AutomaticAdaptiveSettings = false;
      c.EngineSettings.engine = weather_routing::RoutingEngine::Main;
      c.DeltaTime = 600;
      c.ByDegrees = 7.5;
      c.RoutingEffortPercent = 100;
      route->SetConfiguration(c);
      route->Reset();
      routing->ShowResults();
      Reset(true);
      passed &= reset.applied && route->GetConfiguration().DeltaTime == 600 &&
                !setup.m_validationStatus->GetLabel().empty();
      if (!passed) {
        Finish(false);
        return;
      }
      phase = 1;
    } else if (phase == 1) {
      if (routing->RouteComputationActive(route)) return;
      passed &= route->ReachedDestination();
      wxLogMessage("QA_RESET_RACE passed=%d reached=%d", passed,
                   route->ReachedDestination());
      Reset(false);
      passed &= reset.applied && route->GetConfiguration().DeltaTime == 3600 &&
                !route->HasComputedResults() &&
                route->GetPlotData(false).empty();
      const auto c = route->GetConfiguration();
      passed &=
          c.AutomaticAdaptiveSettings == false &&
          c.MaxTrueWindKnots == saved.MaxTrueWindKnots &&
          c.MaxSwellMeters == saved.MaxSwellMeters &&
          c.boatFileName == saved.boatFileName &&
          c.StartTime == saved.StartTime &&
          c.SafetyMarginLand == saved.SafetyMarginLand &&
          c.EngineSettings.autoEngines == saved.EngineSettings.autoEngines &&
          c.EngineSettings.allEngines == saved.EngineSettings.allEngines;
      wxCommandEvent compute;
      routing->OnCompute(compute);
      phase = 2;
    } else if (phase == 2) {
      if (routing->RouteComputationActive(route)) return;
      passed &= route->ReachedDestination();
      wxLogMessage("QA_RESET_RECOMPUTE passed=%d reached=%d", passed,
                   route->ReachedDestination());
      const auto c = route->GetConfiguration();
      Reset(false, true);
      passed &= !reset.applied && route->HasComputedResults() &&
                route->ReachedDestination() &&
                route->GetConfiguration().EngineSettings == c.EngineSettings;
      routing->ClearComputedResults({route});
      auto custom = route->GetConfiguration();
      custom.DeltaTime = 600;
      route->SetConfiguration(custom);
      setup.SetConfigurations({custom});
      setup.m_cEnginePreset->SetSelection(0);
      wxCommandEvent compute;
      routing->OnCompute(compute);
      wxStopWatch watch;
      wxCommandEvent event;
      setup.OnResetAdvanced(event);
      passed &= watch.Time() < 500 &&
                route->GetConfiguration().DeltaTime == 600 &&
                !setup.m_bResetAdvanced->IsEnabled();
      for (size_t p = 0; p < setup.m_notebook7->GetPageCount(); ++p)
        passed &= !setup.m_notebook7->GetPage(p)->IsEnabled();
      setup.m_sMaxTrueWindKnots->SetValue(1);
      wxSpinEvent spin(wxEVT_SPINCTRL, setup.m_sMaxTrueWindKnots->GetId());
      spin.SetEventObject(setup.m_sMaxTrueWindKnots);
      setup.OnUpdateSpin(spin);
      passed &=
          route->GetConfiguration().MaxTrueWindKnots == custom.MaxTrueWindKnots;
      setup.m_cbAutomaticAdaptiveSettings->SetValue(true);
      wxCommandEvent tick(wxEVT_CHECKBOX,
                          setup.m_cbAutomaticAdaptiveSettings->GetId());
      tick.SetEventObject(setup.m_cbAutomaticAdaptiveSettings);
      setup.m_cbAutomaticAdaptiveSettings->GetEventHandler()->ProcessEvent(
          tick);
      passed &= !route->GetConfiguration().AutomaticAdaptiveSettings;
      setup.SetConfigurations({route->GetConfiguration()});
      wxLogMessage("QA_RESET_QUEUED_EDIT_GUARDS passed=%d", passed);
      phase = 3;
    } else if (phase == 3) {
      if (routing->RouteComputationActive(route)) return;
      passed &= route->ReachedDestination();
      setup.SetConfigurations({route->GetConfiguration()});
      for (size_t p = 0; p < setup.m_notebook7->GetPageCount(); ++p)
        passed &= setup.m_notebook7->GetPage(p)->IsEnabled();
      passed &= setup.m_bResetAdvanced->IsEnabled();
      wxLogMessage("QA_RESET_FINAL passed=%d reached=%d", passed,
                   route->ReachedDestination());
      // A confirmation's captured target must be revalidated after UI events.
      const auto original = route->GetConfiguration();
      auto copy = original;
      routing->AddConfiguration(copy);
      routing->ShowResults();
      routing->m_ResultsWorkspace->Select(routing->m_WeatherRoutes.size() - 1);
      passed &=
          !setup.CanResetRoutes({route}) && route->ReachedDestination() &&
          route->GetConfiguration().EngineSettings == original.EngineSettings;
      routing->DeleteRouteMaps({route});
      passed &= !setup.CanResetRoutes({route});
      wxLogMessage("QA_RESET_STALE_TARGETS passed=%d", passed);
      Finish(passed);
    }
  }
  void Finish(bool ok) {
    timer.Stop();
    reset.Stop();
    std::ofstream xid(std::string(std::getenv("WORKSPACE_QA_DIR")) +
                      "/host-window.txt");
    xid << GDK_WINDOW_XID(
        gtk_widget_get_window(wxTheApp->GetTopWindow()->GetHandle()));
    xid.close();
    std::ofstream out(std::string(std::getenv("WORKSPACE_QA_DIR")) +
                      "/result.json");
    out << "{\"passed\":" << (ok ? "true" : "false") << ",\"phase\":" << phase
        << "}";
    out.close();
  }
};
extern "C" opencpn_plugin* production_create_pi(void* manager);
extern "C" opencpn_plugin* create_pi(void* manager) {
  auto* result = production_create_pi(manager);
  new Probe();
  return result;
}
