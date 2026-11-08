// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "PassageReadiness.h"

namespace wr = weather_routing;
namespace {
wr::PassageInputs Ready() {
  wr::PassageInputs input;
  input.positionsValid = input.distinctPositions = input.timeValid = true;
  input.boatValid = input.forecastLoaded = input.departureWind =
      input.destinationWind = true;
  input.wavesAvailable = true;
  return input;
}
TEST(PassageReadiness, ValidInputsPermitCalculationWithoutAdditionalChoices) {
  EXPECT_TRUE(wr::EvaluatePassage(Ready()).CanCalculate());
  EXPECT_EQ(wr::EvaluatePassage(Ready()).Warnings(), 0U);
}
TEST(PassageReadiness, RequiredInputsBlockAndIdentifyTheirRecoveryAction) {
  auto input = Ready();
  input.positionsValid = false;
  auto result = wr::EvaluatePassage(input);
  ASSERT_FALSE(result.CanCalculate());
  ASSERT_FALSE(result.sections[0].items.empty());
  EXPECT_EQ(wr::RecoveryAction(result.sections[0].items[0].code),
            wr::PassageAction::Route);
  input = Ready();
  input.boatValid = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input = Ready();
  input.timeValid = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input = Ready();
  input.distinctPositions = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, ClimatologyCanWorkWithoutASelectedForecast) {
  auto input = Ready();
  input.forecastLoaded = input.departureWind = false;
  input.policy = wr::PassageWindPolicy::Climatology;
  input.climatologyWind = true;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.climatologyWind = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness,
     FallbackIsExplicitAndUnavailableFallbackDoesNotInventWind) {
  auto input = Ready();
  input.forecastLoaded = input.departureWind = false;
  input.climatologyWind = true;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.policy = wr::PassageWindPolicy::ForecastWithClimatology;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  EXPECT_GT(wr::EvaluatePassage(input).Warnings(), 0U);
  input.climatologyWind = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, MissingWavesWarnOrBlockAccordingToActualPolicy) {
  auto input = Ready();
  input.wavesAvailable = false;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  EXPECT_GT(wr::EvaluatePassage(input).Warnings(), 0U);
  input.requireWaves = true;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.waveLimit = 0;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness,
     IncompleteSpatialAndEstimatedTemporalCoverageAreProvisionalWarnings) {
  auto input = Ready();
  input.destinationWind = false;
  input.forecastEnd = wxDateTime(time_t(1000000));
  input.estimatedFinish = wxDateTime(time_t(1003600));
  auto result = wr::EvaluatePassage(input);
  EXPECT_TRUE(result.CanCalculate());
  EXPECT_EQ(result.Warnings(), 2U);
}
TEST(PassageReadiness, ChangedRouteSequenceRequiresANewLinkedPassage) {
  auto input = Ready();
  input.routeSequenceValid = false;
  auto result = wr::EvaluatePassage(input);
  EXPECT_FALSE(result.CanCalculate());
  EXPECT_EQ(result.sections[0].items[0].code, "passage.route.sequence");
}
TEST(PassageReadiness,
     ExampleBoatAndDisabledLandAvoidanceRemainVisibleWarnings) {
  auto input = Ready();
  input.exampleBoat = true;
  input.avoidLand = false;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_TRUE(result.CanCalculate());
  EXPECT_EQ(result.Warnings(), 2U);
  EXPECT_EQ(wr::PassageSectionStatus(result.sections[1]), "Needs review");
}
}  // namespace

TEST(PassageReadiness,
     UnsetPassageDoesNotMisreportClimatologyCoverageAsMissing) {
  auto input = Ready();
  input.coverageChecked = false;
  input.positionsValid = false;
  input.policy = wr::PassageWindPolicy::Climatology;
  input.climatologyWind = false;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_FALSE(result.CanCalculate());
  EXPECT_FALSE(result.sections[2].HasErrors());
  EXPECT_EQ(wr::PassageSectionStatus(result.sections[2]), "Awaiting passage");
}

TEST(PassageReadiness, NoWindPolicyDoesNotMasqueradeAsClimatology) {
  auto input = Ready();
  input.policy = wr::PassageWindPolicy::None;
  input.climatologyWind = true;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_FALSE(result.CanCalculate());
  EXPECT_EQ(result.sections[2].items[0].code, "passage.weather.source");
}
TEST(PassageReadiness, DisabledLimitsPreserveExistingEngineSemantics) {
  auto input = Ready();
  input.windLimit = input.apparentWindLimit = input.waveLimit = 0;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_TRUE(result.CanCalculate());
  EXPECT_EQ(result.sections[3].items[0].code, "passage.limits.disabled");
  input.windLimit = -1;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, MotoringRequiresPositiveSpeedOnlyWhenEnabled) {
  auto input = Ready();
  input.motorSpeed = 0;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.useMotor = true;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.motorSpeed = 5;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, AShorelineOnlyHostCannotEnforceChartedDepth) {
  auto input = Ready();
  input.minimumDepth = 5;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.chartDepthAvailable = true;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.minimumDepth = -1;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
