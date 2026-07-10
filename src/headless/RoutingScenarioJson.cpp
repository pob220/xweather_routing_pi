/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "RoutingScenarioJson.h"

#include <fstream>
#include <memory>
#include <sstream>

#include <json/json.h>
#include <wx/filename.h>

namespace {

wxString JsonString(const Json::Value& value, const char* key,
                    const wxString& fallback = wxEmptyString) {
  if (!value.isMember(key) || !value[key].isString()) return fallback;
  return wxString::FromUTF8(value[key].asCString());
}

bool JsonDouble(const Json::Value& value, const char* key, double& out) {
  if (!value.isMember(key) || !value[key].isNumeric()) return false;
  out = value[key].asDouble();
  return true;
}

bool JsonInt(const Json::Value& value, const char* key, int& out) {
  if (!value.isMember(key) || !value[key].isInt()) return false;
  out = value[key].asInt();
  return true;
}

bool JsonBool(const Json::Value& value, const char* key, bool& out) {
  if (!value.isMember(key) || !value[key].isBool()) return false;
  out = value[key].asBool();
  return true;
}

wxString TimeToJson(const wxDateTime& time) {
  if (!time.IsValid()) return wxEmptyString;
  return time.FormatISOCombined('T') + "Z";
}

void AddOptionalLong(Json::Value& parent, const char* key, long value) {
  if (value >= 0) parent[key] = Json::Int64(value);
}

void AddOptionalDouble(Json::Value& parent, const char* key, double value) {
  if (value >= 0.0) parent[key] = value;
}

}  // namespace

namespace weather_routing_headless {

bool LoadRoutingScenarioJson(
    const wxString& path,
    weather_routing_engine::RoutingScenario& scenario,
    wxString& error) {
  std::ifstream input(path.mb_str());
  if (!input.good()) {
    error = wxString::Format("cannot open scenario file: %s", path);
    return false;
  }

  Json::Value root;
  Json::Reader reader;
  if (!reader.parse(input, root, false) || !root.isObject()) {
    error = wxString::Format("invalid scenario JSON in %s: %s", path,
                             wxString::FromUTF8(
                                 reader.getFormattedErrorMessages().c_str()));
    return false;
  }

  if (root.isMember("schemaVersion") && root["schemaVersion"].isInt())
    scenario.schemaVersion = root["schemaVersion"].asInt();
  scenario.name = JsonString(root, "name", "Weather Routing Scenario");

  const Json::Value& start = root["start"];
  const Json::Value& end = root["end"];
  if (!start.isObject() || !end.isObject()) {
    error = "scenario must contain start and end objects";
    return false;
  }
  scenario.start.name = JsonString(start, "name", "Start");
  scenario.end.name = JsonString(end, "name", "End");
  if (!JsonDouble(start, "lat", scenario.start.lat) ||
      !JsonDouble(start, "lon", scenario.start.lon) ||
      !JsonDouble(end, "lat", scenario.end.lat) ||
      !JsonDouble(end, "lon", scenario.end.lon)) {
    error = "scenario start/end must contain numeric lat/lon";
    return false;
  }

  wxString start_time = JsonString(root, "startTime");
  if (!start_time.IsEmpty()) {
    wxString normalized = start_time;
    if (normalized.EndsWith("Z")) normalized.RemoveLast();
    if (!scenario.startTime.ParseISOCombined(normalized, 'T')) {
      error = wxString::Format("invalid scenario startTime: %s", start_time);
      return false;
    }
  }

  const Json::Value& opt = root["departureOptimization"];
  if (opt.isObject()) {
    JsonBool(opt, "enabled", scenario.departureOptimization.enabled);
    JsonInt(opt, "beforeMinutes",
            scenario.departureOptimization.beforeMinutes);
    JsonInt(opt, "afterMinutes", scenario.departureOptimization.afterMinutes);
    JsonInt(opt, "stepMinutes", scenario.departureOptimization.stepMinutes);
    if (scenario.departureOptimization.stepMinutes <= 0)
      scenario.departureOptimization.stepMinutes = 60;
  }

  const Json::Value& environment = root["environment"];
  if (environment.isObject()) {
    bool use_currents = false;
    if (JsonBool(environment, "useCurrents", use_currents)) {
      scenario.environment.useCurrents = use_currents;
      scenario.environment.hasUseCurrents = true;
    }
  }

  const Json::Value& safety = root["safety"];
  if (safety.isObject()) {
    scenario.safety.mode = JsonString(safety, "mode", scenario.safety.mode);
    bool bool_value = false;
    double double_value = 0.0;
    if (JsonBool(safety, "enforce", bool_value)) {
      scenario.safety.enforce = bool_value;
      scenario.safety.hasEnforce = true;
    }
    if (JsonDouble(safety, "landMarginNm", double_value)) {
      scenario.safety.landMarginNm = double_value;
      scenario.safety.hasLandMarginNm = true;
    }
    if (JsonDouble(safety, "minimumDepthM", double_value)) {
      scenario.safety.minimumDepthM = double_value;
      scenario.safety.hasMinimumDepthM = true;
    }
    if (JsonBool(safety, "persistentCertifiedCacheEnabled", bool_value)) {
      scenario.safety.persistentCertifiedCacheEnabled = bool_value;
      scenario.safety.hasPersistentCertifiedCacheEnabled = true;
    }
  }

  const Json::Value& reverse = root["reverseReachability"];
  if (reverse.isObject()) {
    bool bool_value = false;
    int int_value = 0;
    double double_value = 0.0;
    if (JsonBool(reverse, "enabled", bool_value))
      scenario.reverseReachability.enabled = bool_value;
    if (JsonInt(reverse, "searchBackIsochrones", int_value)) {
      scenario.reverseReachability.searchBackIsochrones = int_value;
      scenario.reverseReachability.hasSearchBackIsochrones = true;
    }
    if (JsonDouble(reverse, "horizonHours", double_value)) {
      scenario.reverseReachability.horizonHours = double_value;
      scenario.reverseReachability.hasHorizonHours = true;
    }
    if (JsonBool(reverse, "diagnostics", bool_value)) {
      scenario.reverseReachability.diagnostics = bool_value;
      scenario.reverseReachability.hasDiagnostics = true;
    }
  }

  return true;
}

bool SaveRoutingResultJson(
    const wxString& path,
    const weather_routing_engine::RoutingResult& result,
    wxString& error) {
  Json::Value root;
  root["schemaVersion"] = result.schemaVersion;
  root["scenario"] = result.scenario.ToUTF8().data();
  root["status"] = result.status.ToUTF8().data();
  if (!result.failureReason.IsEmpty())
    root["failureReason"] = result.failureReason.ToUTF8().data();

  Json::Value candidates(Json::arrayValue);
  for (const auto& candidate : result.candidates) {
    Json::Value value;
    if (candidate.departure.IsValid())
      value["departure"] = TimeToJson(candidate.departure).ToUTF8().data();
    value["state"] = candidate.state.ToUTF8().data();
    if (candidate.eta.IsValid())
      value["eta"] = TimeToJson(candidate.eta).ToUTF8().data();
    AddOptionalLong(value, "elapsed", candidate.elapsedSeconds);
    AddOptionalDouble(value, "distanceNm", candidate.distanceNm);
    value["finalSafety"] = candidate.finalSafety.ToUTF8().data();
    if (!candidate.failureReason.IsEmpty())
      value["failureReason"] = candidate.failureReason.ToUTF8().data();
    value["offsetMinutes"] = candidate.offsetMinutes;
    value["reverseRecoveryUsed"] = candidate.reverseRecoveryUsed;
    if (!candidate.reverseRecoveryStatus.IsEmpty())
      value["reverseRecoveryStatus"] =
          candidate.reverseRecoveryStatus.ToUTF8().data();
    AddOptionalLong(value, "reverseLayersBuilt",
                    candidate.reverseLayersBuilt);
    AddOptionalLong(value, "reverseNodesGenerated",
                    candidate.reverseNodesGenerated);
    AddOptionalLong(value, "reverseNodesFeasible",
                    candidate.reverseNodesFeasible);
    value["reverseConnectionFound"] = candidate.reverseConnectionFound;
    if (candidate.reverseConnectionTime.IsValid())
      value["reverseConnectionTime"] =
          TimeToJson(candidate.reverseConnectionTime).ToUTF8().data();
    if (!candidate.reverseFailureReason.IsEmpty())
      value["reverseFailureReason"] =
          candidate.reverseFailureReason.ToUTF8().data();
    value["reverseFinalValidationPass"] =
        candidate.reverseFinalValidationPass;
    candidates.append(value);
  }
  root["candidates"] = candidates;

  Json::Value diagnostics(Json::objectValue);
  AddOptionalLong(diagnostics, "generatedMoves", result.diagnostics.generatedMoves);
  AddOptionalLong(diagnostics, "acceptedMoves", result.diagnostics.acceptedMoves);
  AddOptionalLong(diagnostics, "chartLandRejections",
                  result.diagnostics.chartLandRejections);
  AddOptionalLong(diagnostics, "missingSafetyTiles",
                  result.diagnostics.missingSafetyTiles);
  AddOptionalLong(diagnostics, "fineTilesBuilt", result.diagnostics.fineTilesBuilt);
  AddOptionalLong(diagnostics, "persistentCacheHits",
                  result.diagnostics.persistentCacheHits);
  AddOptionalLong(diagnostics, "chartApiCallsFromWorkers",
                  result.diagnostics.chartApiCallsFromWorkers);
  AddOptionalLong(diagnostics, "finalValidationMs",
                  result.diagnostics.finalValidationMs);
  root["diagnostics"] = diagnostics;

  wxFileName filename(path);
  if (!filename.GetPath().IsEmpty() && !filename.DirExists()) {
    if (!filename.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
      error = wxString::Format("cannot create output directory: %s",
                               filename.GetPath());
      return false;
    }
  }

  std::ofstream output(path.mb_str(), std::ios::out | std::ios::trunc);
  if (!output.good()) {
    error = wxString::Format("cannot open result file: %s", path);
    return false;
  }

  Json::StyledWriter writer;
  output << writer.write(root);
  if (!output.good()) {
    error = wxString::Format("failed writing result file: %s", path);
    return false;
  }
  return true;
}

}  // namespace weather_routing_headless
