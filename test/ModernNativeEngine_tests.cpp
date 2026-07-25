#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <vector>

#include "supercpn/weather_routing/Engine.h"

namespace {
using namespace supercpn::weather_routing;

TimePoint TestTime() { return TimePoint{Duration{1784419200}}; }

PerformanceProfile TestSailingProfile(double speed = 6.0) {
  PerformanceProfile profile;
  profile.role = ProfileRole::SailOnly;
  profile.identity = "native-engine-test";
  profile.rows = {{5.0,
                   {{0.0, 0.0},
                    {35.0, speed * 0.55},
                    {45.0, speed * 0.75},
                    {90.0, speed},
                    {135.0, speed},
                    {180.0, speed * 0.85}}},
                  {15.0,
                   {{0.0, 0.0},
                    {35.0, speed * 0.65},
                    {45.0, speed * 0.9},
                    {90.0, speed * 1.15},
                    {135.0, speed * 1.1},
                    {180.0, speed * 0.95}}}};
  return profile;
}

RoutingRequest TestRequest() {
  RoutingRequest request;
  request.start = {53.341, -4.620887};
  request.destination = {53.311102, -6.122185};
  request.departure = TestTime();
  request.vessel.profiles = {TestSailingProfile()};
  request.vessel.tackPenalty = std::chrono::minutes{5};
  request.vessel.gybePenalty = std::chrono::minutes{5};
  request.environment.missingCurrent = MissingCurrentPolicy::AllowAssumedZero;
  request.environment.zeroCurrentAcknowledged = true;
  request.environment.missingWaves = MissingWavePolicy::AllowWithWarning;
  request.constraints.maximumTrueWindKnots = 50.0;
  request.options.timeStep = std::chrono::minutes{30};
  request.options.minimumTimeStep = std::chrono::minutes{10};
  request.options.headingStepDegrees = 15.0;
  request.options.refinedHeadingStepDegrees = 5.0;
  request.options.maximumSearchAngleDegrees = 180.0;
  request.options.destinationToleranceNm = 0.75;
  request.options.spatialCellNm = 2.0;
  request.options.labelsPerCell = 6;
  request.limits.maximumRouteDuration = std::chrono::hours{72};
  request.limits.maximumGeneratedStates = 500000;
  request.limits.maximumRetainedStates = 100000;
  request.limits.maximumGraphLabels = 100000;
  return request;
}

std::shared_ptr<UniformWeatherProvider> TestWeather() {
  UniformWeatherProvider::Configuration configuration;
  configuration.identity = "irish-sea-uniform";
  configuration.source = EnvironmentalSource::SyntheticTestField;
  configuration.windTowardKnots = speedDirectionToVector(14.0, 140.0);
  configuration.currentTowardKnots = Vector2{};
  WaveSample wave;
  wave.available = true;
  wave.significantHeightMetres = 0.8;
  wave.directionFromDegrees = 300.0;
  wave.periodSeconds = 7.0;
  configuration.wave = wave;
  configuration.begins = TestTime();
  configuration.ends = TestTime() + std::chrono::hours{96};
  return std::make_shared<UniformWeatherProvider>(configuration);
}

RoutingEnvironment TestEnvironment(
    std::shared_ptr<const LandAndBoundaryProvider> boundaries =
        std::make_shared<OpenWaterProvider>()) {
  RoutingEnvironment environment;
  environment.grib = TestWeather();
  environment.landAndBoundaries = std::move(boundaries);
  return environment;
}

class BlockingMeridianProvider final : public LandAndBoundaryProvider {
public:
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint start, GeoPoint end, double) const override {
    return (start.longitude > -5.5 && end.longitude <= -5.5) ||
           (start.longitude <= -5.5 && end.longitude > -5.5);
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return std::abs(point.longitude + 5.5) * 60.0;
  }
  std::string identity() const override { return "blocking-meridian"; }
};

class RecordingTimedBoundaryProvider final : public LandAndBoundaryProvider {
public:
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    ++untimedCalls;
    return false;
  }
  bool segmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint, TimePoint time,
                                       double) const override {
    observedTimes.push_back(time);
    return false;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override { return "recording-timed-boundary"; }

  mutable std::vector<TimePoint> observedTimes;
  mutable unsigned untimedCalls{};
};

class TimeGatePerformance final : public VesselPerformanceModel {
public:
  explicit TimeGatePerformance(TimePoint opens) : opens_(opens) {}

  bool valid(std::string*) const override { return true; }
  std::vector<PerformanceCandidate> candidates(double, double,
                                               const WaveSample&,
                                               PropulsionMode,
                                               Duration) const override {
    return {};
  }
  PerformanceCandidate evaluate(PropulsionMode, ProfileRole, const std::string&,
                                double, double,
                                const WaveSample&) const override {
    return {};
  }
  std::vector<PerformanceCandidate> candidatesAt(GeoPoint, TimePoint time,
                                                 double, double,
                                                 const WaveSample&,
                                                 PropulsionMode,
                                                 Duration) const override {
    if (time < opens_) return {};
    return {{true, PropulsionMode::Sail, ProfileRole::SailOnly, "time-gate", 0,
             6.0, 0.0}};
  }
  PerformanceCandidate evaluateAt(GeoPoint, TimePoint time, PropulsionMode,
                                  ProfileRole, const std::string&, double,
                                  double, const WaveSample&) const override {
    if (time < opens_) return {};
    return {
        true, PropulsionMode::Sail, ProfileRole::SailOnly, "time-gate", 0, 6.0,
        0.0};
  }

private:
  TimePoint opens_;
};

bool Successful(RoutingStatus status) {
  return status == RoutingStatus::Complete ||
         status == RoutingStatus::CompleteUsingReverseRecovery ||
         status == RoutingStatus::CompleteUsingGraphFallback;
}

TEST(ModernNativeEngine, RoutesIrishSeaDeterministically) {
  RoutingEngine engine;
  const auto first = engine.route(TestRequest(), TestEnvironment());
  const auto second = engine.route(TestRequest(), TestEnvironment());
  ASSERT_TRUE(Successful(first.status)) << first.message;
  ASSERT_TRUE(first.validation.passed) << first.validation.failureReason;
  EXPECT_EQ(first.metrics.elapsed, second.metrics.elapsed);
  EXPECT_EQ(first.legs.size(), second.legs.size());
  ASSERT_FALSE(first.legs.empty());
  EXPECT_EQ(first.legs.back().end, TestRequest().destination);
}

TEST(ModernNativeEngine, RejectsAChartBlockedPassage) {
  auto request = TestRequest();
  request.options.maximumSearchAngleDegrees = 80.0;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  const auto result = RoutingEngine{}.route(
      request, TestEnvironment(std::make_shared<BlockingMeridianProvider>()));
  EXPECT_FALSE(Successful(result.status));
}

TEST(ModernNativeEngine, SuppliesChronologicalTimesToDynamicBoundaries) {
  auto boundaries = std::make_shared<RecordingTimedBoundaryProvider>();
  const auto result =
      RoutingEngine{}.route(TestRequest(), TestEnvironment(boundaries));
  ASSERT_TRUE(Successful(result.status)) << result.message;
  ASSERT_FALSE(boundaries->observedTimes.empty());
  EXPECT_TRUE(std::all_of(boundaries->observedTimes.begin(),
                          boundaries->observedTimes.end(),
                          [](TimePoint time) { return time >= TestTime(); }));
  EXPECT_TRUE(std::any_of(boundaries->observedTimes.begin(),
                          boundaries->observedTimes.end(),
                          [](TimePoint time) { return time > TestTime(); }));
}

TEST(ModernNativeEngine, UsesRecoveryCascadeAndIndependentValidation) {
  auto request = TestRequest();
  request.options.forceForwardFailureForTesting = true;
  const auto result = RoutingEngine{}.route(request, TestEnvironment());
  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_NE(result.solverPath, SolverPath::AdaptiveIsochrone);
  EXPECT_TRUE(result.validation.passed);
  EXPECT_GT(result.diagnostics.validationSamples, 0U);
}

TEST(ModernNativeEngine, FavourableCurrentMayExceedPolarSpeed) {
  auto weather = TestWeather();
  UniformWeatherProvider::Configuration configuration;
  configuration.identity = "strong-current";
  configuration.source = EnvironmentalSource::SyntheticTestField;
  configuration.windTowardKnots = speedDirectionToVector(14.0, 140.0);
  configuration.currentTowardKnots = speedDirectionToVector(5.0, 270.0);
  configuration.begins = TestTime();
  configuration.ends = TestTime() + std::chrono::hours{96};
  RoutingEnvironment environment;
  environment.grib =
      std::make_shared<UniformWeatherProvider>(std::move(configuration));
  environment.landAndBoundaries = std::make_shared<OpenWaterProvider>();
  const auto result = RoutingEngine{}.route(TestRequest(), environment);
  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_GT(result.metrics.distanceNm * 3600.0 /
                static_cast<double>(result.metrics.elapsed.count()),
            6.0);
}

TEST(ModernNativeEngine, WaitsWithinBoundForAWeatherOrTidalGate) {
  auto request = TestRequest();
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.options.maximumWait = std::chrono::hours{2};
  RoutingEnvironment environment = TestEnvironment();
  environment.performance = std::make_shared<TimeGatePerformance>(
      request.departure + std::chrono::hours{1});
  const auto result = RoutingEngine{}.route(request, environment);
  ASSERT_TRUE(Successful(result.status)) << result.message;
  ASSERT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_GE(result.metrics.waitingTime, std::chrono::hours{1});
  EXPECT_GT(result.diagnostics.waitStates, 0U);
  EXPECT_TRUE(
      std::any_of(result.legs.begin(), result.legs.end(),
                  [](const RouteLeg& leg) { return leg.stationaryWait; }));
}

TEST(ModernNativeEngine, RejectsGateBeyondConfiguredWaitingBound) {
  auto request = TestRequest();
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.options.maximumWait = std::chrono::minutes{30};
  RoutingEnvironment environment = TestEnvironment();
  environment.performance = std::make_shared<TimeGatePerformance>(
      request.departure + std::chrono::hours{2});
  const auto result = RoutingEngine{}.route(request, environment);
  EXPECT_FALSE(Successful(result.status));
}

TEST(ModernNativeEngine, FinalApproachMayOutlastSeveralSearchSteps) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 2.2);
  request.vessel.profiles = {TestSailingProfile(3.0)};
  request.options.timeStep = std::chrono::minutes{10};
  request.options.minimumTimeStep = std::chrono::minutes{10};
  request.options.destinationToleranceNm = 0.35;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumGeneratedStates = 20;

  const auto result = RoutingEngine{}.route(request, TestEnvironment());
  ASSERT_TRUE(Successful(result.status)) << result.message;
  ASSERT_TRUE(result.validation.passed) << result.validation.failureReason;
  ASSERT_FALSE(result.legs.empty());
  EXPECT_EQ(result.legs.back().end, request.destination);
  EXPECT_GT(result.metrics.elapsed, std::chrono::minutes{30});
  EXPECT_LE(result.diagnostics.generatedStates, 20U);
}

}  // namespace
