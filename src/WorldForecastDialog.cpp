// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldForecastDialog.h"

#ifndef __OCPN__ANDROID__
#include <algorithm>
#include <array>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "WorldGribModels.h"
#include "XgribPaths.h"
#include "environmental_grib/area_validation.h"
#include "weather_routing_pi.h"

namespace weather_routing {
namespace {

const char* const kModelSetting = "/PlugIns/xWeatherRouting2/WorldModel";

class WorldDialog final : public wxDialog {
public:
  WorldDialog(wxWindow* parent, const xgrib::AreaPreset& area, int hours,
              const std::optional<xgrib::AreaPreset>& chartArea)
      : wxDialog(parent, wxID_ANY, _("Download a World forecast")),
        cache_(wxFileName(weather_routing_pi::StandardPath(),
                          kWorldGribModelsFile).GetFullPath()),
        chartArea_(chartArea) {
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _("Model")), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    model_ = new wxChoice(this, wxID_ANY);
    model_->SetName(_("World forecast model"));
    row->Add(model_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    refresh_ = new wxButton(this, wxID_ANY, _("Update Models"));
    row->Add(refresh_);
    layout->Add(row, 0, wxALL | wxEXPAND, 12);

    auto* grid = new wxFlexGridSizer(2, 6, 12);
    grid->AddGrowableCol(1);
    const wxString labels[] = {_("West longitude"), _("South latitude"),
                               _("East longitude"), _("North latitude")};
    for (size_t i = 0; i < coordinates_.size(); ++i) {
      grid->Add(new wxStaticText(this, wxID_ANY, labels[i]), 0,
                wxALIGN_CENTER_VERTICAL);
      coordinates_[i] = new wxTextCtrl(this, wxID_ANY);
      coordinates_[i]->SetName(labels[i]);
      grid->Add(coordinates_[i], 1, wxEXPAND);
    }
    grid->Add(new wxStaticText(this, wxID_ANY, _("Forecast length (hours)")),
              0, wxALIGN_CENTER_VERTICAL);
    hours_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxDefaultSize, wxSP_ARROW_KEYS, 1,
                            std::max(hours, 1), std::max(hours, 1));
    hours_->SetName(_("Forecast length in hours"));
    grid->Add(hours_, 0, wxEXPAND);
    layout->Add(grid, 0, wxLEFT | wxRIGHT | wxEXPAND, 12);

    chart_ = new wxButton(this, wxID_ANY, _("Use chart area"));
    layout->Add(chart_, 0, wxLEFT | wxTOP, 12);
    const auto note = _("Hours count from the model run. Current-only "
                        "forecasts open in the combined GRIB generator.");
    auto* information = new wxStaticText(this, wxID_ANY, note);
    information->Wrap(440);
    layout->Add(information, 0, wxALL | wxEXPAND, 12);
    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    status_->SetMinSize(wxSize(440, -1));
    layout->Add(status_, 0, wxLEFT | wxRIGHT | wxEXPAND, 12);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    download_ = new wxButton(this, wxID_ANY, _("Download"));
    cancel_ = new wxButton(this, wxID_ANY, _("Cancel transfer"));
    auto* close = new wxButton(this, wxID_CANCEL, _("Close"));
    buttons->Add(download_, 0, wxRIGHT, 8);
    buttons->Add(cancel_, 0, wxRIGHT, 8);
    buttons->Add(close);
    layout->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 12);

    SetArea(area);
    wxString selected = "ecmwf0p25";
    if (auto* config = GetOCPNConfigObject())
      config->Read(kModelSetting, &selected, selected);
    SetModels(LoadWorldGribModels(cache_,
                                  GetXgribDataDirectory() + kWorldGribModelsFile),
              selected);
    SetSizerAndFit(layout);

#ifndef __WXMSW__
    transferEvents_.Bind(wxEventTypeTag<OCPN_downloadEvent>(wxEVT_DOWNLOAD_EVENT),
                         &WorldDialog::OnTransfer, this);
#endif
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { CloseDialog(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CloseDialog(); });
    chart_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetArea(*chartArea_); });
    model_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateHours(); });
    refresh_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RefreshModels(); });
    download_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Download(); });
    cancel_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      Cancel();
      SetStatus(_("Transfer cancelled."));
    });
  }

  ~WorldDialog() override { Cancel(); }
  WorldForecastDownload result;

private:
  enum class Transfer { Idle, Models, Forecast };

  void SetArea(const xgrib::AreaPreset& area) {
    const double values[] = {area.west, area.south, area.east, area.north};
    for (size_t i = 0; i < coordinates_.size(); ++i)
      coordinates_[i]->ChangeValue(wxString::Format("%.5f", values[i]));
  }

  void SetStatus(const wxString& status) {
    status_->SetLabelText(status);
    status_->Wrap(440);
    Layout();
    const auto minimum = GetSizer()->CalcMin();
    SetMinClientSize(minimum);
    // Error details can wrap onto more lines than the initial status.
    const auto size = GetClientSize();
    if (size.x < minimum.x || size.y < minimum.y)
      SetClientSize(wxSize(std::max(size.x, minimum.x),
                           std::max(size.y, minimum.y)));
  }

  void SetModels(std::vector<WorldGribModel> models, const wxString& selected) {
    models_ = std::move(models);
    model_->Clear();
    int index = 0;
    for (const auto& item : models_) {
      const int row = model_->Append(item.title);
      if (wxString::FromUTF8(item.id.c_str()) == selected) index = row;
    }
    if (!models_.empty()) {
      model_->SetSelection(index);
      UpdateHours();
    }
    EnableControls();
  }

  void UpdateHours() {
    const int selection = model_->GetSelection();
    if (selection == wxNOT_FOUND) return;
    const int maxHours = models_[selection].maxHours;
    const int value = hours_->GetValue();
    hours_->SetRange(1, maxHours);
    hours_->SetValue(std::clamp(value, 1, maxHours));
  }

  void EnableControls() {
    const bool idle = transfer_ == Transfer::Idle;
    for (auto* coordinate : coordinates_) coordinate->Enable(idle);
    chart_->Enable(idle && chartArea_.has_value());
    model_->Enable(idle && !models_.empty());
    hours_->Enable(idle && !models_.empty());
    download_->Enable(idle && !models_.empty());
    refresh_->Enable(idle);
    cancel_->Enable(!idle);
  }

  void DiscardPartial() {
    if (!partial_.empty() && wxFileExists(partial_)) wxRemoveFile(partial_);
    partial_.clear();
  }

  void Cancel() {
    if (transfer_ == Transfer::Idle) return;
    transfer_ = Transfer::Idle;
#ifndef __WXMSW__
    OCPN_cancelDownloadFileBackground(handle_);
    transferEvents_.DeletePendingEvents();
#endif
    DiscardPartial();
    EnableControls();
  }

  void CloseDialog() {
    Cancel();
    EndModal(wxID_CANCEL);
  }

  void Start(Transfer transfer, const wxString& url, const wxString& target) {
    partial_ = wxFileName::CreateTempFileName(target + ".part-");
    if (partial_.empty()) {
      SetStatus(_("Could not create a temporary file in the forecast folder."));
      return;
    }
    transfer_ = transfer;
    destination_ = target;
    EnableControls();
    SetStatus(transfer == Transfer::Models ? _("Updating the model list…")
                                           : _("Requesting forecast…"));
#ifdef __WXMSW__
    // Stock Windows OpenCPN exports the progress-dialog API but does not
    // provide the download event data symbol in its plugin import library.
    const auto outcome = OCPN_downloadFile(
        url, partial_, _("Downloading World forecast"), status_->GetLabelText(),
        wxNullBitmap, this,
        OCPN_DLDS_ELAPSED_TIME | OCPN_DLDS_SPEED | OCPN_DLDS_SIZE |
            OCPN_DLDS_CAN_ABORT | OCPN_DLDS_AUTO_CLOSE,
        180);
    if (outcome == OCPN_DL_ABORTED) {
      Cancel();
      SetStatus(_("Transfer cancelled."));
      return;
    }
    FinishTransfer(outcome == OCPN_DL_NO_ERROR);
#else
    handle_ = 0;
    if (OCPN_downloadFileBackground(url, partial_, &transferEvents_, &handle_) !=
        OCPN_DL_STARTED) {
      transfer_ = Transfer::Idle;
      DiscardPartial();
      EnableControls();
      SetStatus(_("The download could not start. Try again."));
    }
#endif
  }

  void RefreshModels() {
    Start(Transfer::Models, wxString::FromUTF8(kWorldGribModelsUrl), cache_);
  }

  void Download() {
    double fields[4]{};
    for (size_t i = 0; i < coordinates_.size(); ++i) {
      if (coordinates_[i]->GetValue().ToDouble(&fields[i])) continue;
      SetStatus(_("Enter all four coordinates as decimal degrees."));
      coordinates_[i]->SetFocus();
      return;
    }
    const auto issue = environmental_grib::ValidateDownloadArea(
        fields[0], fields[1], fields[2], fields[3]);
    if (issue.message) {
      SetStatus(wxString::FromUTF8(issue.message));
      return;
    }
    const int selected = model_->GetSelection();
    if (selected == wxNOT_FOUND) return;
    selected_ = models_[selected];
    requestedArea_.west = fields[0];
    requestedArea_.south = fields[1];
    requestedArea_.east = fields[2];
    requestedArea_.north = fields[3];
    const int duration = hours_->GetValue();
    const auto url = WorldGribUrl(selected_, duration, fields[1], fields[3],
                                  fields[0], fields[2]);
    wxFileName folder = wxFileName::DirName(weather_routing_pi::StandardPath());
    folder.AppendDir("grib");
    folder.AppendDir("generated");
    if (!folder.DirExists() && !folder.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
      SetStatus(_("Could not create the forecast folder."));
      return;
    }
    const auto filename = wxString::Format(
        "wr_world_%s_%s.grb2", wxString::FromUTF8(selected_.id.c_str()),
        wxDateTime::Now().ToUTC().Format("%Y%m%d_%H%M%S"));
    Start(Transfer::Forecast, wxString::FromUTF8(url.c_str()),
          wxFileName(folder.GetPath(), filename).GetFullPath());
  }

#ifndef __WXMSW__
  void OnTransfer(OCPN_downloadEvent& event) {
    if (transfer_ == Transfer::Idle) return;
    if (event.getDLEventCondition() == OCPN_DL_EVENT_TYPE_PROGRESS) {
      if (event.getTransferred() > 0)
        SetStatus(wxString::Format(_("Downloaded %.1f MiB"),
                                   event.getTransferred() / 1048576.0));
      return;
    }
    if (event.getDLEventCondition() != OCPN_DL_EVENT_TYPE_END) return;
    FinishTransfer(event.getDLEventStatus() == OCPN_DL_NO_ERROR);
  }
#endif

  void FinishTransfer(bool received) {
    const auto completed = transfer_;
    transfer_ = Transfer::Idle;
    if (completed == Transfer::Models && received) {
      auto models = ReadWorldGribModels(partial_);
      if (!models.empty() && wxRenameFile(partial_, cache_, true)) {
        const int chosen = model_->GetSelection();
        const auto id = chosen == wxNOT_FOUND
                            ? wxString()
                            : wxString::FromUTF8(models_[chosen].id.c_str());
        SetModels(std::move(models), id);
        SetStatus(_("Model list updated."));
      } else {
        SetStatus(_("Could not save a valid model list."));
      }
    } else if (completed == Transfer::Forecast && received &&
               HasWorldGribFileMarkers(partial_)) {
      if (wxRenameFile(partial_, destination_, false)) {
        DiscardPartial();
        result = {destination_, IsWorldCurrentModel(selected_), requestedArea_};
        if (auto* config = GetOCPNConfigObject())
          config->Write(kModelSetting, wxString::FromUTF8(selected_.id.c_str()));
        EndModal(wxID_OK);
        return;
      }
      SetStatus(_("Could not save the downloaded forecast."));
    } else {
      const auto reason = WorldGribServerError(partial_);
      SetStatus(!reason.empty() ? wxString::Format(_("Download failed: %s"), reason)
                : received ? _("The server did not return a complete GRIB file.")
                           : _("Download failed. Try again."));
    }
    DiscardPartial();
    EnableControls();
  }

  std::array<wxTextCtrl*, 4> coordinates_{};
  wxChoice* model_;
  wxSpinCtrl* hours_;
  wxButton *chart_, *download_, *refresh_, *cancel_;
  wxStaticText* status_;
  std::vector<WorldGribModel> models_;
  WorldGribModel selected_;
  xgrib::AreaPreset requestedArea_;
  wxString cache_, partial_, destination_;
  std::optional<xgrib::AreaPreset> chartArea_;
  Transfer transfer_ = Transfer::Idle;
#ifndef __WXMSW__
  wxEvtHandler transferEvents_;
  long handle_ = 0;
#endif
};

}  // namespace

std::optional<WorldForecastDownload> ChooseWorldForecast(
    wxWindow* parent, const xgrib::AreaPreset& area, int hours,
    const std::optional<xgrib::AreaPreset>& chartArea) {
  WorldDialog dialog(parent, area, hours, chartArea);
  if (dialog.ShowModal() != wxID_OK) return std::nullopt;
  return dialog.result;
}

}  // namespace weather_routing
#endif
