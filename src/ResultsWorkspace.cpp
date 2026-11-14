// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __OCPN__ANDROID__
#include <wx/wx.h>
#include "ResultsWorkspace.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "RoutingTablePanel.h"
#include "PlanningHelp.h"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/fileconf.h>

ResultsWorkspace::ResultsWorkspace(WeatherRouting& routing)
    : wxDialog(&routing, wxID_ANY, _("Passage results"), wxDefaultPosition,
               wxSize(920, 650), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      routing_(routing) {
  SetName("Passage results workspace");
  auto* outer = new wxBoxSizer(wxVERTICAL);
  auto* nav = new wxBoxSizer(wxHORIZONTAL);
  passages_ = new wxChoice(this, wxID_ANY);
  passages_->SetName("Results passage selection");
  nav->Add(passages_, 1, wxEXPAND | wxALL, 5);
  const auto button = [this](wxSizer* row, const wxString& label, auto action) {
    auto* control = new wxButton(this, wxID_ANY, label);
    row->Add(control, 0, wxALL, 5);
    control->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
    return control;
  };
  button(nav, _("Previous"), [this] { Select(passages_->GetSelection() - 1); });
  button(nav, _("Next"), [this] { Select(passages_->GetSelection() + 1); });
  button(nav, _("Plan passage"), [this] { routing_.ShowPlanningPage(false); Hide(); });
  outer->Add(nav, 0, wxEXPAND);
  selection_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
  outer->Add(selection_, 0, wxEXPAND | wxALL, 5);
  pages_ = new wxNotebook(this, wxID_ANY);
  pages_->SetName("Passage result views");
  table_ = new RoutingTablePanel(pages_, routing_, nullptr);
  pages_->AddPage(table_, _("Table"));
#ifndef __OCPN__ANDROID__
  Embed(routing_.m_PlotDialog, _("Plot"));
  Embed(routing_.m_StatisticsDialog, _("Statistics"));
  Embed(routing_.m_ReportDialog, _("Report"));
#endif
  auto* compare = new wxPanel(pages_);
  auto* compareSizer = new wxBoxSizer(wxVERTICAL);
  auto* explanation = new wxStaticText(compare, wxID_ANY,
      _("Compare completed candidates or inspect retained partial routes for the selected passage. Choosing a result remains an explicit action."));
  explanation->Wrap(600);
  compareSizer->Add(explanation, 0, wxEXPAND | wxALL, 12);
  for (bool partial : {false, true}) {
    auto* action = new wxButton(compare, wxID_ANY,
        partial ? _("Inspect partial routes...") : _("Compare fastest / comfort..."));
    compareSizer->Add(action, 0, wxALL, 8);
    action->Bind(wxEVT_BUTTON, [this, partial](wxCommandEvent&) {
      routing_.ShowRouteComparison(partial);
    });
  }
  compare->SetSizer(compareSizer);
  pages_->AddPage(compare, _("Compare"));
  auto* safety = new wxPanel(pages_);
  auto* safetySizer = new wxBoxSizer(wxVERTICAL);
  safety_ = new wxTextCtrl(safety, wxID_ANY, wxEmptyString, wxDefaultPosition,
      wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
  safety_->SetName("Selected passage safety record");
  safetySizer->Add(safety_, 1, wxEXPAND | wxALL, 8);
  auto* review = new wxButton(safety, wxID_ANY, _("Review safety settings..."));
  safetySizer->Add(review, 0, wxALL, 8);
  review->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    routing_.m_ConfigurationDialog.ShowProblem("passage.limits.land");
  });
  safety->SetSizer(safetySizer);
  pages_->AddPage(safety, _("Safety"));
  outer->Add(pages_, 1, wxEXPAND | wxALL, 5);
  auto* footer = new wxBoxSizer(wxHORIZONTAL);
  button(footer, _("Help"), [this] {
    weather_routing::ShowPlanningHelp(this, weather_routing::PlanningHelpTopic::Navigation);
  });
  footer->AddStretchSpacer();
  button(footer, _("Close"), [this] { SaveGeometry(); Hide(); });
  outer->Add(footer, 0, wxEXPAND);
  SetSizer(outer);
  passages_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { Select(passages_->GetSelection()); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { SaveGeometry(); Hide(); });
  auto* config = GetOCPNConfigObject();
  config->SetPath("/PlugIns/xWeatherRouting2/ResultsWorkspace");
  long x = 80, y = 80, width = 920, height = 650;
  config->Read("X", &x); config->Read("Y", &y);
  config->Read("Width", &width); config->Read("Height", &height);
  SetMinSize(FromDIP(wxSize(600, 400)));
  SetSize(x, y, width, height);
  weather_routing::KeepPlanningDialogVisible(this);
}

void ResultsWorkspace::Embed(wxDialog& dialog, const wxString& name) {
  dialog.Hide();
  auto* contents = dialog.GetSizer();
  auto* page = new wxPanel(pages_);
  dialog.SetSizer(nullptr, false);
  std::vector<wxWindow*> children;
  for (auto* child : dialog.GetChildren()) if (!child->IsTopLevel()) children.push_back(child);
  for (auto* child : children) child->Reparent(page);
  page->SetSizer(contents);
  // The workspace supplies one Close action. Retain every content action,
  // including copy diagnostics and each report's internal notebook.
  const auto hideClose = [](auto&& self, wxWindow* parent) -> void {
    for (auto* child : parent->GetChildren()) {
      if (auto* button = wxDynamicCast(child, wxButton))
        if (button->GetId() == wxID_CANCEL || button->GetId() == wxID_OK || wxControl::RemoveMnemonics(button->GetLabel()) == _("Close")) button->Hide();
      self(self, child);
    }
  };
  hideClose(hideClose, page);
  pages_->AddPage(page, name);
  embedded_.push_back({&dialog, page, contents});
}

ResultsWorkspace::~ResultsWorkspace() {
  SaveGeometry();
  ClearRouteReferences();
  for (auto& entry : embedded_) {
    entry.page->SetSizer(nullptr, false);
    std::vector<wxWindow*> children;
    for (auto* child : entry.page->GetChildren()) children.push_back(child);
    for (auto* child : children) child->Reparent(entry.dialog);
    entry.dialog->SetSizer(entry.contents);
  }
}

void ResultsWorkspace::ClearRouteReferences() {
  if (table_) table_->SetRouteMap(nullptr);
  routing_.m_PlotDialog.SetRouteMapOverlay(nullptr);
  routes_.clear();
}

void ResultsWorkspace::Select(int index) {
  if (refreshing_ || index < 0 || size_t(index) >= routes_.size()) return;
  auto* selected = routes_[index];
  if (!routing_.RouteMapIsManaged(selected)) { RefreshSelection(); return; }
  for (long row = 0; row < routing_.m_panel->m_lWeatherRoutes->GetItemCount(); ++row) {
    auto* item = reinterpret_cast<WeatherRoute*>(wxUIntToPtr(
        routing_.m_panel->m_lWeatherRoutes->GetItemData(row)));
    routing_.m_panel->m_lWeatherRoutes->SetItemState(row,
        item && item->routemapoverlay == selected ? wxLIST_STATE_SELECTED : 0,
        wxLIST_STATE_SELECTED);
  }
  routing_.OnWeatherRouteSelected();
  RefreshSelection();
}

void ResultsWorkspace::RefreshSelection() {
  if (refreshing_) return;
  refreshing_ = true;
  auto* current = routing_.FirstCurrentRouteMap();
  routes_.clear();
  passages_->Clear();
  int selected = wxNOT_FOUND;
  for (auto* item : routing_.m_WeatherRoutes) {
    if (item->routemapoverlay == current) selected = routes_.size();
    routes_.push_back(item->routemapoverlay);
    const auto c = item->routemapoverlay->GetConfiguration();
    passages_->Append(c.Start + " to " + c.End);
  }
  passages_->SetSelection(selected);
  const auto routes = routing_.CurrentRouteMaps();
  selection_->SetLabel(routes.size() > 1
      ? wxString::Format(_("%zu passages selected. Table and Plot show the first; Statistics and Report cover the selection."), routes.size())
      : current ? _("Results for the selected passage") : _("Select a passage in Routes"));
  selection_->Wrap(std::max(300, GetClientSize().x - 20));
  selection_->SetMinSize(wxSize(1, -1));
  table_->SetRouteMap(current);
  routing_.m_PlotDialog.SetRouteMapOverlay(current);
  routing_.m_StatisticsDialog.SetRouteMapOverlays(routes);
  routing_.m_ReportDialog.SetRouteMapOverlays(routes);
  wxString safety = _("No passage selected.");
  if (current) {
    const auto c = current->GetConfiguration();
    safety = c.DetectLand ? _("Land avoidance enabled.\n") : _("Land avoidance disabled.\n");
    safety += wxString::Format(_("Requested land clearance: %.2f NM; minimum charted depth: %.1f m.\n"), c.SafetyMarginLand, c.MinimumDepthMeters);
    safety += _("These are requested settings. The final validation record below reports what was checked. Shoreline checks alone do not establish charted-depth safety.\n\n");
    safety += current->GetDiagnostics().Text();
  }
  safety_->ChangeValue(safety);
  Layout();
  refreshing_ = false;
}

void ResultsWorkspace::Open(size_t page) {
  RefreshSelection();
  if (page < pages_->GetPageCount()) pages_->SetSelection(page);
  weather_routing::KeepPlanningDialogVisible(this);
  Show(true); Raise();
}

void ResultsWorkspace::SaveGeometry() {
  auto* config = GetOCPNConfigObject();
  config->SetPath("/PlugIns/xWeatherRouting2/ResultsWorkspace");
  config->Write("X", GetPosition().x); config->Write("Y", GetPosition().y);
  config->Write("Width", GetSize().x); config->Write("Height", GetSize().y);
}

void ResultsWorkspace::UpdateTimeHighlight(wxDateTime time) {
  if (table_) table_->UpdateTimeHighlight(time);
}

bool ResultsWorkspace::RunHostContract() {
  RefreshSelection();
  bool passed = pages_->GetPageCount() == 6 &&
      table_->GetRouteMap() == routing_.FirstCurrentRouteMap();
  auto* saved = routing_.FirstCurrentRouteMap();
  if (saved && saved->ReachedDestination() && !saved->GetPlotData(false).empty()) {
    auto* summary = wxDynamicCast(wxWindow::FindWindowByName("Route table summary", table_), wxStaticText);
    wxString text = summary ? summary->GetLabel() : wxString(); text.Replace("\n", " ");
    passed &= text.Contains(routing_.m_SettingsDialog.FormatTime(saved->EndTime(), "%Y-%m-%d %H:%M"));
  }
  for (size_t i = 0; i < pages_->GetPageCount(); ++i) {
    pages_->SetSelection(i); Layout();
    passed &= routing_.FirstCurrentRouteMap() == saved;
  }
  if (routes_.size() > 1) {
    Select((passages_->GetSelection() + 1) % routes_.size());
    passed &= table_->GetRouteMap() == routing_.FirstCurrentRouteMap();
  }
  ClearRouteReferences();
  passed &= table_->GetRouteMap() == nullptr;
  RefreshSelection();
  passed &= table_->GetRouteMap() == routing_.FirstCurrentRouteMap();
  return passed;
}

#endif
