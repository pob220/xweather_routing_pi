#include <wx/wx.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include <json/json.h>

#include "headless/RoutingScenarioJson.h"

TEST(RoutingScenarioJson, LoadsStabilityCorridorOptions) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_dunlaoghaire_stability.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  EXPECT_TRUE(scenario.stabilityCorridor.enabled);
  EXPECT_EQ(6, scenario.departureOptimization.concurrentRoutes);
  EXPECT_EQ("departureCandidates", scenario.stabilityCorridor.source);
  EXPECT_EQ(3, scenario.stabilityCorridor.minimumRoutes);
  EXPECT_DOUBLE_EQ(120.0, scenario.stabilityCorridor.maxEtaPenaltyMinutes);
  EXPECT_DOUBLE_EQ(0.5, scenario.stabilityCorridor.gridResolutionNm);
  EXPECT_DOUBLE_EQ(0.7, scenario.stabilityCorridor.innerAgreementThreshold);
  EXPECT_DOUBLE_EQ(0.4, scenario.stabilityCorridor.outerAgreementThreshold);
  EXPECT_TRUE(scenario.stabilityCorridor.clusterRoutes);
  EXPECT_TRUE(scenario.stabilityCorridor.writeGeoJson);
}

TEST(RoutingScenarioJson, LoadsSelfContainedGuiRegressionSettings) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_dunlaoghaire_gui_regression.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  EXPECT_TRUE(scenario.environment.hasUseGrib);
  EXPECT_TRUE(scenario.environment.useGrib);
  EXPECT_TRUE(scenario.environment.useCurrents);
  EXPECT_TRUE(scenario.route.hasBoatFile);
  EXPECT_EQ(
      "~/.opencpn/plugins/weather_routing/boats/"
      "Nicholson_35_conservative.xml",
      scenario.route.boatFile);
  EXPECT_EQ(3600, scenario.route.timeStepSeconds);
  EXPECT_DOUBLE_EQ(40.0, scenario.route.headingFromDegrees);
  EXPECT_DOUBLE_EQ(160.0, scenario.route.headingToDegrees);
  EXPECT_DOUBLE_EQ(5.0, scenario.route.headingStepDegrees);
  EXPECT_TRUE(scenario.route.optimizeTacking);
  EXPECT_DOUBLE_EQ(1.0, scenario.route.upwindEfficiency);
  EXPECT_DOUBLE_EQ(1.0, scenario.route.downwindEfficiency);
  EXPECT_DOUBLE_EQ(1.0, scenario.route.nightEfficiency);
  EXPECT_TRUE(scenario.route.useMotor);
  EXPECT_DOUBLE_EQ(4.5, scenario.route.motorSpeedThresholdKnots);
  EXPECT_DOUBLE_EQ(5.5, scenario.route.motorSpeedKnots);
}

TEST(RoutingScenarioJson, WritesStabilitySummaryAsValidJson) {
  weather_routing_engine::RoutingResult result;
  result.scenario = "Stability JSON test";
  result.status = "complete";
  result.stabilityCorridor.requested = true;
  result.stabilityCorridor.status = "complete";
  result.stabilityCorridor.validRoutes = 7;
  result.stabilityCorridor.excludedRoutes = 2;
  result.stabilityCorridor.routeFamilies = 2;
  result.stabilityCorridor.selectedFamilyId = 1;
  result.stabilityCorridor.dominantFamilyRoutes = 4;
  result.stabilityCorridor.medianWidthNm = 2.4;
  result.stabilityCorridor.maximumWidthNm = 8.7;
  result.stabilityCorridor.etaSpreadMinutes = 43.0;
  result.stabilityCorridor.innerThreshold = 0.7;
  result.stabilityCorridor.outerThreshold = 0.4;
  result.stabilityCorridor.representativeCandidateId = "candidate-0";
  result.stabilityCorridor.geoJsonPath = "/tmp/result.stability.geojson";

  const wxString path = "/tmp/weather-routing-stability-result.json";
  wxString error;
  ASSERT_TRUE(
      weather_routing_headless::SaveRoutingResultJson(path, result, error))
      << error;
  std::ifstream input(path.mb_str());
  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(reader.parse(input, root, false));
  ASSERT_TRUE(root["stabilityCorridor"].isObject());
  EXPECT_EQ(7, root["stabilityCorridor"]["validRoutes"].asInt());
  EXPECT_EQ(2, root["stabilityCorridor"]["routeFamilies"].asInt());
  EXPECT_EQ("candidate-0",
            root["stabilityCorridor"]["representativeCandidateId"].asString());
  std::remove(path.mb_str());
}
