#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <limits>
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

class MeridianBarrierWithOpenEndsProvider final
    : public LandAndBoundaryProvider {
public:
  MeridianBarrierWithOpenEndsProvider(double longitude, double centreLatitude,
                                      double halfHeightDegrees)
      : longitude_(longitude),
        centreLatitude_(centreLatitude),
        halfHeightDegrees_(halfHeightDegrees) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint start, GeoPoint end, double) const override {
    const double longitudeDelta = end.longitude - start.longitude;
    if (std::abs(longitudeDelta) < 1e-12) return false;
    const double fraction = (longitude_ - start.longitude) / longitudeDelta;
    if (fraction < 0.0 || fraction > 1.0) return false;
    const double crossingLatitude =
        start.latitude + fraction * (end.latitude - start.latitude);
    return std::abs(crossingLatitude - centreLatitude_) <= halfHeightDegrees_;
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return std::abs(point.longitude - longitude_) * 60.0;
  }
  std::string identity() const override {
    return "meridian-barrier-with-open-ends";
  }

private:
  double longitude_;
  double centreLatitude_;
  double halfHeightDegrees_;
};

RoutingRequest GraphDetourRequest(double maximumCorridorWidthNm) {
  auto request = TestRequest();
  request.start = {0.0, 0.0};
  request.destination = {0.0, 0.2};
  request.options.graphHeadingStepDegrees = 10.0;
  request.options.maximumSearchAngleDegrees = 180.0;
  request.options.spatialCellNm = 0.5;
  request.options.forceForwardFailureForTesting = true;
  request.options.forceReverseFailureForTesting = true;
  request.options.graphCorridorWidthNm = 1.0;
  request.options.maximumGraphCorridorWidthNm = maximumCorridorWidthNm;
  // The synthetic field has no favourable current, so this is an admissible
  // A* bound and keeps the recovery-specific regression tests compact.
  request.options.heuristicMaximumSpeedKnots = 20.0;
  request.limits.maximumRouteDuration = std::chrono::hours{6};
  request.limits.maximumGeneratedStates = 100000;
  request.limits.maximumRetainedStates = 30000;
  request.limits.maximumGraphLabels = 50000;
  return request;
}

class ConstantSpeedPerformance final : public VesselPerformanceModel {
public:
  bool valid(std::string*) const override { return true; }
  std::vector<PerformanceCandidate> candidates(double, double,
                                               const WaveSample&,
                                               PropulsionMode,
                                               Duration) const override {
    return {{true, PropulsionMode::Sail, ProfileRole::SailOnly, "constant", 0,
             8.0, 0.0}};
  }
  PerformanceCandidate evaluate(PropulsionMode, ProfileRole, const std::string&,
                                double, double,
                                const WaveSample&) const override {
    return {true, PropulsionMode::Sail, ProfileRole::SailOnly, "constant", 0,
            8.0, 0.0};
  }
  std::vector<PerformanceCandidate> candidatesAt(
      GeoPoint, TimePoint, double, double, const WaveSample& wave,
      PropulsionMode mode, Duration duration) const override {
    return candidates(0.0, 0.0, wave, mode, duration);
  }
  PerformanceCandidate evaluateAt(
      GeoPoint, TimePoint, PropulsionMode mode, ProfileRole role,
      const std::string& identity, double, double,
      const WaveSample& wave) const override {
    return evaluate(mode, role, identity, 0.0, 0.0, wave);
  }
};

RoutingEnvironment GraphDetourEnvironment(
    std::shared_ptr<const LandAndBoundaryProvider> boundaries =
        std::make_shared<OpenWaterProvider>()) {
  auto environment = TestEnvironment(std::move(boundaries));
  environment.performance = std::make_shared<ConstantSpeedPerformance>();
  return environment;
}

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

class SplitSearchValidationProvider final : public LandAndBoundaryProvider {
public:
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    ++searchCalls;
    return false;
  }
  bool segmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint, TimePoint,
                                       double) const override {
    ++searchCalls;
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint, TimePoint,
                                                 double) const override {
    ++validationCalls;
    validationObservedPreparedRoute = prepareCalls > 0;
    return true;
  }
  void prepareValidationRoute(std::span<const RouteLeg> legs,
                              double safetyMarginNm) const override {
    ++prepareCalls;
    preparedLegs = legs.size();
    preparedSafetyMarginNm = safetyMarginNm;
  }
  bool validationFailureRequiresSearchEscalation() const override {
    return true;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override {
    return "open-search-blocked-validation";
  }

  mutable unsigned searchCalls{};
  mutable unsigned prepareCalls{};
  mutable unsigned validationCalls{};
  mutable std::size_t preparedLegs{};
  mutable double preparedSafetyMarginNm{};
  mutable bool validationObservedPreparedRoute{};
};

class DestinationValidationBlockProvider final
    : public LandAndBoundaryProvider {
public:
  explicit DestinationValidationBlockProvider(GeoPoint destination)
      : destination_(destination) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint end,
                                                 TimePoint,
                                                 double) const override {
    return distanceNm(end, destination_) < 0.8;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override {
    return "destination-validation-block";
  }

private:
  GeoPoint destination_;
};

class CoastalDepartureEgressProvider final : public LandAndBoundaryProvider {
public:
  explicit CoastalDepartureEgressProvider(GeoPoint departure)
      : departure_(departure) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(
      GeoPoint start, GeoPoint, TimePoint,
      double safetyMarginNm) const override {
    // A full-margin chord beginning inside the configured coastal buffer is
    // rejected. The zero-margin egress replay remains clear of actual land.
    return safetyMarginNm > 0.0 && distanceNm(start, departure_) <= 0.002;
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return distanceNm(point, departure_) <= 0.002 ? 0.1 : 10.0;
  }
  std::string identity() const override { return "coastal-departure-egress"; }

private:
  GeoPoint departure_;
};

class CoastalDestinationIngressProvider final : public LandAndBoundaryProvider {
public:
  CoastalDestinationIngressProvider(GeoPoint destination,
                                    bool actualLandCrossing = false)
      : destination_(destination), actualLandCrossing_(actualLandCrossing) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint start, GeoPoint end,
                        double safetyMarginNm) const override {
    return segmentFromKnownSafeForbiddenAt(start, end, TestTime(),
                                           safetyMarginNm);
  }
  bool segmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint end, TimePoint,
                                       double safetyMarginNm) const override {
    if (actualLandCrossing_ && distanceNm(end, destination_) < 0.2) return true;
    return safetyMarginNm > 0.0 && distanceNm(end, destination_) < 0.5;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(
      GeoPoint start, GeoPoint end, TimePoint time,
      double safetyMarginNm) const override {
    return segmentFromKnownSafeForbiddenAt(start, end, time, safetyMarginNm);
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return distanceNm(point, destination_) <= 0.002 ? 0.1 : 10.0;
  }
  std::string identity() const override {
    return "coastal-destination-ingress";
  }

private:
  GeoPoint destination_;
  bool actualLandCrossing_{};
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

class ReplayEndpointGapProvider final : public LandAndBoundaryProvider {
public:
  explicit ReplayEndpointGapProvider(GeoPoint destination)
      : destination_(destination) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(GeoPoint start, GeoPoint end,
                                                 TimePoint,
                                                 double) const override {
    return distanceNm(end, destination_) <= 0.002 &&
           distanceNm(start, end) < 0.2;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override { return "replay-endpoint-gap"; }

private:
  GeoPoint destination_;
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
  EXPECT_EQ(first.validation.acceptedPrefixLegs, first.legs.size());
  EXPECT_EQ(first.metrics.elapsed, second.metrics.elapsed);
  EXPECT_EQ(first.legs.size(), second.legs.size());
  ASSERT_FALSE(first.legs.empty());
  EXPECT_EQ(first.legs.back().end, TestRequest().destination);
}

TEST(ModernNativeEngine, IndependentlyValidatesIncompleteRecoveryPrefixes) {
  const RoutingRequest request = TestRequest();
  const RoutingEnvironment environment = TestEnvironment();
  const auto route = RoutingEngine{}.route(request, environment);
  ASSERT_TRUE(Successful(route.status)) << route.message;
  ASSERT_GT(route.legs.size(), 2U);
  const std::span<const RouteLeg> prefix(route.legs.data(),
                                         route.legs.size() / 2);
  PolarPerformanceModel performance(request.vessel);

  const auto completeValidation =
      RouteValidator{}.validate(request, environment, performance, prefix);
  const auto prefixValidation = RouteValidator{}.validatePrefix(
      request, environment, performance, prefix);

  EXPECT_FALSE(completeValidation.passed);
  EXPECT_TRUE(prefixValidation.passed) << prefixValidation.failureReason;
  EXPECT_EQ(prefixValidation.acceptedPrefixLegs, prefix.size());
}

TEST(ModernNativeEngine,
     DenseReplayAllowsBoundedExploratoryIntegrationDifference) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 6.1);
  RouteLeg leg;
  leg.start = request.start;
  leg.end = request.destination;
  leg.startTime = request.departure;
  leg.endTime = request.departure + std::chrono::hours{1};
  leg.headingDegrees = 270.0;
  leg.courseThroughWaterDegrees = 270.0;
  leg.courseOverGroundDegrees = 270.0;
  leg.speedThroughWaterKnots = 6.0;
  leg.speedOverGroundKnots = 6.1;
  leg.propulsionMode = PropulsionMode::Sail;
  leg.profileRole = ProfileRole::SailOnly;
  leg.profileIdentity = "time-gate";
  TimeGatePerformance performance(request.departure);

  const auto validation =
      RouteValidator{}.validate(request, TestEnvironment(), performance,
                                std::span<const RouteLeg>(&leg, 1));

  EXPECT_TRUE(validation.passed) << validation.failureReason;
  EXPECT_EQ(validation.acceptedPrefixLegs, 1U);
}

TEST(ModernNativeEngine,
     DenseReplayNeverLeavesAnUncheckedEndpointReconciliationGap) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 6.1);
  RouteLeg leg;
  leg.start = request.start;
  leg.end = request.destination;
  leg.startTime = request.departure;
  leg.endTime = request.departure + std::chrono::hours{1};
  leg.headingDegrees = 270.0;
  leg.courseThroughWaterDegrees = 270.0;
  leg.courseOverGroundDegrees = 270.0;
  leg.speedThroughWaterKnots = 6.0;
  leg.speedOverGroundKnots = 6.1;
  leg.propulsionMode = PropulsionMode::Sail;
  leg.profileRole = ProfileRole::SailOnly;
  leg.profileIdentity = "time-gate";
  TimeGatePerformance performance(request.departure);
  auto boundaries =
      std::make_shared<ReplayEndpointGapProvider>(request.destination);

  const auto validation = RouteValidator{}.validate(
      request, TestEnvironment(boundaries), performance,
      std::span<const RouteLeg>(&leg, 1));

  EXPECT_FALSE(validation.passed);
  EXPECT_NE(validation.failureReason.find("endpoint reconciliation"),
            std::string::npos);
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

TEST(ModernNativeEngine, UsesAuthoritativeValidationProviderHook) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries = std::make_shared<SplitSearchValidationProvider>();

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  EXPECT_FALSE(Successful(result.status));
  EXPECT_GT(boundaries->searchCalls, 0U);
  EXPECT_GT(boundaries->prepareCalls, 0U);
  EXPECT_GT(boundaries->preparedLegs, 0U);
  EXPECT_DOUBLE_EQ(boundaries->preparedSafetyMarginNm,
                   request.constraints.landSafetyMarginNm);
  EXPECT_TRUE(boundaries->validationObservedPreparedRoute);
  EXPECT_GT(boundaries->validationCalls, 0U);
  EXPECT_EQ(result.status, RoutingStatus::ValidationFailure);
  EXPECT_NE(result.message.find("detailed-constraint search escalation"),
            std::string::npos);
}

TEST(ModernNativeEngine, SafeRejectedPrefixSkipsWholeRouteRefinement) {
  auto request = TestRequest();
  request.options.useReverseRecovery = false;
  request.limits.maximumGeneratedStates = 500000;
  request.limits.maximumGraphLabels = 1000;
  auto boundaries =
      std::make_shared<DestinationValidationBlockProvider>(request.destination);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  EXPECT_FALSE(Successful(result.status));
  EXPECT_EQ(std::count(result.diagnostics.stagesAttempted.begin(),
                       result.diagnostics.stagesAttempted.end(),
                       SolverPath::AdaptiveIsochrone),
            1);
  EXPECT_TRUE(std::any_of(
      result.diagnostics.stageStopReasons.begin(),
      result.diagnostics.stageStopReasons.end(), [](const std::string& reason) {
        return reason.find("safe recovery prefix") != std::string::npos;
      }));
}

TEST(ModernNativeEngine, AuthoritativeReplayAllowsSafeCoastalDepartureEgress) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries =
      std::make_shared<CoastalDepartureEgressProvider>(request.start);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_EQ(result.validation.acceptedPrefixLegs, result.legs.size());
}

TEST(ModernNativeEngine,
     AuthoritativeReplayAllowsSafeCoastalDestinationIngress) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries =
      std::make_shared<CoastalDestinationIngressProvider>(request.destination);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_EQ(result.validation.acceptedPrefixLegs, result.legs.size());
}

TEST(ModernNativeEngine, CoastalDestinationIngressNeverPermitsLandCrossing) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries = std::make_shared<CoastalDestinationIngressProvider>(
      request.destination, true);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

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

TEST(ModernNativeEngine, GraphFallbackStaysInFastCorridorWhenItCanSolve) {
  auto request = GraphDetourRequest(
      std::numeric_limits<double>::infinity());
  request.options.graphCorridorWidthNm = 5.0;
  const auto result = RoutingEngine{}.route(request, GraphDetourEnvironment());

  ASSERT_TRUE(Successful(result.status))
      << result.message << " generated=" << result.diagnostics.generatedStates
      << " labels=" << result.diagnostics.graphLabels
      << " closest=" << result.diagnostics.closestApproachNm;
  ASSERT_EQ(result.solverPath, SolverPath::GraphFallback);
  ASSERT_EQ(result.diagnostics.graphCorridorWidthsNm.size(), 1U);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm.front(), 5.0);
}

TEST(ModernNativeEngine, GraphFallbackWidensToReachSafeOffCourseRoute) {
  auto request = GraphDetourRequest(5.0);
  auto boundaries = std::make_shared<MeridianBarrierWithOpenEndsProvider>(
      (request.start.longitude + request.destination.longitude) / 2.0,
      (request.start.latitude + request.destination.latitude) / 2.0, 0.03);
  const auto result =
      RoutingEngine{}.route(request, GraphDetourEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status))
      << result.message << " generated=" << result.diagnostics.generatedStates
      << " labels=" << result.diagnostics.graphLabels
      << " closest=" << result.diagnostics.closestApproachNm;
  ASSERT_EQ(result.solverPath, SolverPath::GraphFallback);
  ASSERT_GE(result.diagnostics.graphCorridorWidthsNm.size(), 2U);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm.front(), 1.0);
  EXPECT_GT(result.diagnostics.graphCorridorWidthsNm.back(), 1.0);
  EXPECT_LE(result.diagnostics.graphCorridorWidthsNm.back(), 5.0);
  double maximumCrossTrackNm = 0.0;
  for (const auto& leg : result.legs)
    maximumCrossTrackNm =
        std::max(maximumCrossTrackNm,
                 std::abs(crossTrackDistanceNm(request.start,
                                               request.destination, leg.end)));
  EXPECT_GT(maximumCrossTrackNm, 1.5);
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
}

TEST(ModernNativeEngine, UnboundedFinalGraphStageRemovesGraphOnlyLimit) {
  auto request =
      GraphDetourRequest(std::numeric_limits<double>::infinity());
  auto boundaries = std::make_shared<MeridianBarrierWithOpenEndsProvider>(
      (request.start.longitude + request.destination.longitude) / 2.0,
      (request.start.latitude + request.destination.latitude) / 2.0, 0.055);
  const auto result =
      RoutingEngine{}.route(request, GraphDetourEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status))
      << result.message << " generated=" << result.diagnostics.generatedStates
      << " labels=" << result.diagnostics.graphLabels
      << " closest=" << result.diagnostics.closestApproachNm;
  ASSERT_EQ(result.solverPath, SolverPath::GraphFallback);
  ASSERT_EQ(result.diagnostics.graphCorridorWidthsNm.size(), 3U);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm[0], 1.0);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm[1], 2.0);
  EXPECT_TRUE(std::isinf(result.diagnostics.graphCorridorWidthsNm[2]));
  double maximumCrossTrackNm = 0.0;
  for (const auto& leg : result.legs)
    maximumCrossTrackNm =
        std::max(maximumCrossTrackNm,
                 std::abs(crossTrackDistanceNm(request.start,
                                               request.destination, leg.end)));
  EXPECT_GT(maximumCrossTrackNm, 2.8);
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
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
