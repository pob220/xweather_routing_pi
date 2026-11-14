// SPDX-License-Identifier: GPL-3.0-or-later
#include <wx/wx.h>
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "ResultsWorkspace.h"
#include "PlanningHelp.h"
#include "PassagePanel.h"
#ifndef __OCPN__ANDROID__
#include <wx/wrapsizer.h>
#endif

void WeatherRouting::ShowPlanningPage(bool routes) {
#ifndef __OCPN__ANDROID__
  if (m_PassageNotebook) m_PassageNotebook->SetSelection(routes ? 1 : 0);
  if (!routes && m_PassagePanel) m_PassagePanel->RefreshPassage();
#else
  if (m_androidBook) m_androidBook->ChangeSelection(routes ? 1 : 0);
#endif
  Show(true); Raise();
}

void WeatherRouting::ShowResults(size_t page) {
#ifndef __OCPN__ANDROID__
  if (!m_ResultsWorkspace) m_ResultsWorkspace = new ResultsWorkspace(*this);
  m_ResultsWorkspace->Open(page);
#else
  if (m_androidBook) m_androidBook->ChangeSelection(2);
  RefreshAndroidWorkspace();
#endif
}

void WeatherRouting::BuildRoutesWorkspaceTools(wxWindow* page) {
#ifndef __OCPN__ANDROID__
  auto* bar = new wxPanel(page);
  auto* actions = new wxWrapSizer(wxHORIZONTAL);
  const auto add = [](wxWindow* parent, wxSizer* sizer, const wxString& label, auto callback) {
    auto* button = new wxButton(parent, wxID_ANY, label);
    sizer->Add(button, 0, wxALL, 3);
    button->Bind(wxEVT_BUTTON, [callback](wxCommandEvent&) { callback(); });
  };
  add(bar, actions, _("Plan passage"), [this] { ShowPlanningPage(false); });
  add(bar, actions, _("Results..."), [this] { ShowResults(); });
  add(bar, actions, _("Progress..."), [this] { wxCommandEvent e; OnShowRoutingStatus(e); });
  add(bar, actions, _("Plugin settings..."), [this] { wxCommandEvent e; OnSettings(e); });
  bar->SetSizer(actions);
  page->GetSizer()->Add(bar, 0, wxEXPAND);
  auto* selection = new wxPanel(page);
  auto* selected = new wxWrapSizer(wxHORIZONTAL);
  m_RoutesSelectionLabel = new wxStaticText(selection, wxID_ANY, _("Select a passage"));
  selected->Add(m_RoutesSelectionLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
  add(selection, selected, _("Edit selected..."), [this] { OnEditConfiguration(); });
  add(selection, selected, _("Compare fastest / comfort..."), [this] { ShowRouteComparison(); });
  add(selection, selected, _("Inspect partial routes..."), [this] { ShowRouteComparison(true); });
  selection->SetSizer(selected);
  page->GetSizer()->Add(selection, 0, wxEXPAND);
  m_RoutesSelectionTools = selection;
  auto* group = new wxPanel(page);
  auto* groupActions = new wxWrapSizer(wxHORIZONTAL);
  add(group, groupActions, _("Multi-leg settings..."), [this] { wxCommandEvent e; OnEditMultiLegGroupSettings(e); });
  add(group, groupActions, _("Compute sequence"), [this] { wxCommandEvent e; OnComputeMultiLegSequence(e); });
  add(group, groupActions, _("Optimise group departure..."), [this] { wxCommandEvent e; OnOptimizeMultiLegDeparture(e); });
  group->SetSizer(groupActions);
  page->GetSizer()->Add(group, 0, wxEXPAND);
  m_RoutesMultiLegTools = group;
  RefreshRoutesWorkspaceTools();
#endif
}

void WeatherRouting::RefreshRoutesWorkspaceTools() {
#ifndef __OCPN__ANDROID__
  if (!m_RoutesSelectionTools || !m_RoutesMultiLegTools) return;
  auto* current = FirstCurrentRouteMap();
  m_RoutesSelectionTools->Enable(current != nullptr);
  m_RoutesMultiLegTools->Show(current && current->GetConfiguration().IsMultiLegGenerated);
  if (current) {
    const auto c = current->GetConfiguration();
    m_RoutesSelectionLabel->SetLabel(c.IsMultiLegGenerated
        ? wxString::Format(_("Selected leg %d of %d"), c.MultiLegLegIndex, c.MultiLegLegCount)
        : _("Selected passage"));
  } else m_RoutesSelectionLabel->SetLabel(_("Select a passage"));
  m_RoutesMultiLegTools->GetParent()->Layout();
#endif
}


bool WeatherRouting::RunWorkspaceHostContract() {
  bool passed = true;
#ifndef __OCPN__ANDROID__
  passed &= m_PassageNotebook && m_PassageNotebook->GetPageCount() == 2 &&
      m_PassageNotebook->GetPageText(0) == _("Plan passage") &&
      m_PassageNotebook->GetPageText(1) == _("Routes");
  ShowResults();
  passed &= m_ResultsWorkspace->RunHostContract();
  delete m_ResultsWorkspace;
  m_ResultsWorkspace = nullptr;
  passed &= m_ReportDialog.GetSizer() && m_PlotDialog.GetSizer() && m_StatisticsDialog.GetSizer();
  ShowResults();
  passed &= m_ResultsWorkspace->RunHostContract();
  m_ResultsWorkspace->Hide();
#endif
  passed &= m_ConfigurationDialog.RunWorkspaceHostContract();
  wxLogMessage("WR_WORKSPACE_HOST_CONTRACT passed=%d", passed ? 1 : 0);
  return passed;
}
