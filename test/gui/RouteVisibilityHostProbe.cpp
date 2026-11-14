// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only factory wrapper; never install this library in a user profile.
#include <wx/wx.h>
#include <wx/dcmemory.h>
#include <wx/glcanvas.h>
#include <wx/notebook.h>
#include <wx/timer.h>
#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "pidc.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <fstream>
#include <cstdlib>
#include <memory>

class VisibilityProbe : public wxEvtHandler {
  wxTimer timer_{this};
  WeatherRouting* routing_{};
  RouteMapOverlay* route_{};
  wxFrame* glFrame_{};
  wxGLCanvas* canvas_{};
  std::unique_ptr<wxGLContext> context_;
  int phase_ = 0, ticks_ = 0;
  bool passed_ = true;

  void Select(int row) {
    auto* list = routing_->m_panel->m_lWeatherRoutes;
    for (long i = 0; i < list->GetItemCount(); ++i)
      list->SetItemState(i, i == row ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
  }

  wxImage Draw(PlugIn_ViewPort& vp, bool gl) {
    if (!gl) {
      wxBitmap bitmap(vp.pix_width, vp.pix_height, 24);
      wxMemoryDC memory(bitmap);
      memory.SetBackground(*wxWHITE_BRUSH);
      memory.Clear();
      piDC dc(memory);
      routing_->Render(dc, vp);
      memory.SelectObject(wxNullBitmap);
      return bitmap.ConvertToImage();
    }
    canvas_->SetCurrent(*context_);
    glViewport(0, 0, vp.pix_width, vp.pix_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, vp.pix_width, vp.pix_height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    piDC dc;
    dc.SetVP(&vp);
    routing_->Render(dc, vp);
    glFinish();
    wxImage image(vp.pix_width, vp.pix_height);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, vp.pix_width, vp.pix_height, GL_RGB, GL_UNSIGNED_BYTE, image.GetData());
    passed_ &= glGetError() == GL_NO_ERROR;
    return image.Mirror(false);
  }

  bool Check(const char* stage) {
    const auto cf = route_->GetConfiguration();
    PlugIn_ViewPort vp{};
    vp.clat = 53.4; vp.clon = -5.15; vp.view_scale_ppm = .005;
    vp.pix_width = 800; vp.pix_height = 500;
    vp.rv_rect = wxRect(0, 0, 800, 500);
    vp.m_projection_type = 1; vp.bValid = true;
    wxPoint2DDouble start, end;
    GetDoubleCanvasPixLL(&vp, &start, cf.StartLat, cf.StartLon);
    GetDoubleCanvasPixLL(&vp, &end, cf.EndLat, cf.EndLon);
    bool ok = true;
    for (bool gl : {false, true}) {
      if (gl && !context_) continue;
      // Include Rick's exact case: another routing selected, this eye on.
      for (int mode = 0; mode < 5; ++mode) {
        const bool visible = mode < 3;
        routing_->SetRouteVisibility(route_, visible, false);
        Select(mode == 0 || mode == 3 ? 0 : mode == 2 ? -1 : 1);
        wxImage image = Draw(vp, gl);
        const auto count = [&](wxPoint2DDouble point, bool blue) {
          int pixels = 0;
          for (int y = wxRound(point.m_y) - 12; y <= wxRound(point.m_y) + 12; ++y)
            for (int x = wxRound(point.m_x) - 12; x <= wxRound(point.m_x) + 12; ++x) {
              if (x < 0 || x >= 800 || y < 0 || y >= 500) continue;
              const auto r = image.GetRed(x, y), g = image.GetGreen(x, y), b = image.GetBlue(x, y);
              if (blue ? b > 200 && r < 60 && g < 60 : r > 200 && g < 60 && b < 60) ++pixels;
            }
          return pixels;
        };
        const int blue = count(start, true), red = count(end, false);
        int ink = 0;
        for (int y = 0; y < 500; ++y)
          for (int x = 0; x < 800; ++x)
            if (image.GetRed(x, y) != 255 || image.GetGreen(x, y) != 255 || image.GetBlue(x, y) != 255) ++ink;
        const bool expected = visible ? blue > 30 && red > 30 : ink == 0;
        ok &= expected;
        wxLogMessage("QA_VISIBILITY stage=%s renderer=%s mode=%d eye=%d start_blue=%d end_red=%d ink=%d passed=%d",
                     stage, gl ? "GL" : "DC", mode, visible, blue, red, ink, expected);
        image.SaveFile(wxString(std::getenv("WORKSPACE_QA_DIR")) + "/" + stage +
                       wxString::Format("-%s-%d.png", gl ? "gl" : "dc", mode));
      }
    }
    Select(0);
    routing_->SetRouteVisibility(route_, true, false);
    return ok;
  }

  void Tick(wxTimerEvent&) {
    if (++ticks_ > 10000) { Finish(false); return; }
    if (!routing_) {
      for (auto* w : wxTopLevelWindows)
        if (auto* frame = wxDynamicCast(w, wxFrame); frame && frame->GetTitle().Contains("WeatherRouting")) {
          routing_ = static_cast<WeatherRouting*>(frame); break;
        }
      if (!routing_ || routing_->m_WeatherRoutes.empty()) return;
      route_ = routing_->m_WeatherRoutes.front()->routemapoverlay;
    }
    if (phase_ == 0) {
      const auto findBook = [](auto&& self, wxWindow* parent) -> wxNotebook* {
        if (auto* book = wxDynamicCast(parent, wxNotebook)) return book;
        for (auto* child : parent->GetChildren()) if (auto* book = self(self, child)) return book;
        return nullptr;
      };
      auto* book = findBook(findBook, &routing_->m_ConfigurationDialog);
      passed_ &= book && book->GetPageCount() == 5 && book->GetPageText(4) == _("Routing methods");
      auto& settings = routing_->m_SettingsDialog;
      settings.m_sAlternateRouteThickness->SetValue(0);
      settings.m_sIsoChronThickness->SetValue(0);
      settings.m_cbDisplayWindBarbs->SetValue(false);
      settings.m_cbDisplayCurrent->SetValue(false);
      settings.m_cpDestinationRoute->SetColour(wxColour(0, 130, 0));
      // A separate, hidden route provides an unrelated selected row.
      routing_->AddPosition(53.55, -4.2, "Other start");
      routing_->AddPosition(53.55, -4.1, "Other end");
      auto other = route_->GetConfiguration();
      other.Start = "Other start"; other.End = "Other end";
      routing_->AddConfiguration(other);
      routing_->SetRouteVisibility(routing_->m_WeatherRoutes.back()->routemapoverlay, false, false);
      if (std::getenv("WR_VISIBILITY_GL")) {
        int attributes[] = {WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 16, 0};
        glFrame_ = new wxFrame(nullptr, wxID_ANY, "Private visibility OpenGL probe");
        glFrame_->SetClientSize(800, 500);
        canvas_ = new wxGLCanvas(glFrame_, wxID_ANY, attributes, wxPoint(0, 0), wxSize(800, 500));
        context_ = std::make_unique<wxGLContext>(canvas_);
        if (!context_->IsOK()) { Finish(false); return; }
        glFrame_->Show();
      }
      ++phase_; return;
    }
    if (phase_ == 1) {
      passed_ &= Check("before");
      wxCommandEvent compute;
      routing_->OnCompute(compute);
      ++phase_; return;
    }
    if (phase_ == 2) {
      if (!route_->Running()) return;
      passed_ &= Check("running");
      ++phase_; return;
    }
    if (phase_ == 3) {
      if (routing_->RouteComputationActive(route_)) return;
      passed_ &= route_->ReachedDestination();
      passed_ &= Check("complete");
      Finish(passed_);
    }
  }

  void Finish(bool ok) {
    timer_.Stop();
    context_.reset();
    if (glFrame_) { glFrame_->Destroy(); glFrame_ = nullptr; }
    std::ofstream window(std::string(std::getenv("WORKSPACE_QA_DIR")) + "/host-window.txt");
    window << GDK_WINDOW_XID(gtk_widget_get_window(wxTheApp->GetTopWindow()->GetHandle()));
    window.close();
    std::ofstream out(std::string(std::getenv("WORKSPACE_QA_DIR")) + "/result.json");
    out << "{\"passed\":" << (ok ? "true" : "false") << ",\"phase\":" << phase_ << "}";
  }

 public:
  VisibilityProbe() { Bind(wxEVT_TIMER, &VisibilityProbe::Tick, this); timer_.Start(10); }
};

extern "C" opencpn_plugin* production_create_pi(void*);
extern "C" opencpn_plugin* create_pi(void* manager) {
  auto* result = production_create_pi(manager);
  new VisibilityProbe();
  return result;
}
