// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/dialog.h>
#include <wx/datetime.h>
#include <vector>
class WeatherRouting;
class RoutingTablePanel;
class wxChoice;
class wxNotebook;
class wxStaticText;
class wxTextCtrl;
class wxPanel;
class wxSizer;
class RouteMapOverlay;

// Owns the desktop presentation, never routes or solver state. The original
// dialogs retain their controls and handlers; their layouts are restored before
// the containing WeatherRouting object destroys those dialog members.
class ResultsWorkspace : public wxDialog {
 public:
  explicit ResultsWorkspace(WeatherRouting& routing);
  ~ResultsWorkspace() override;
  void Open(size_t page);
  void RefreshSelection();
  void ClearRouteReferences();
  bool RunHostContract();
  void UpdateTimeHighlight(wxDateTime time);
 private:
  struct Embedded { wxDialog* dialog; wxPanel* page; wxSizer* contents; };
  void Embed(wxDialog& dialog, const wxString& name);
  void Select(int index);
  void RefreshActivePage();
  void SaveGeometry();
  WeatherRouting& routing_;
  wxChoice* passages_{};
  wxNotebook* pages_{};
  wxStaticText* selection_{};
  wxTextCtrl* safety_{};
  RoutingTablePanel* table_{};
  std::vector<Embedded> embedded_;
  std::vector<RouteMapOverlay*> routes_;
  bool refreshing_{false};
};
