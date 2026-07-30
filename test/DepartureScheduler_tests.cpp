#include <gtest/gtest.h>

#include "DepartureScheduler.h"

TEST(DepartureScheduler, RunsNominalThenNearestCandidatesDeterministically) {
  std::vector<int> offsets{180, -60, 0, 60, -180, 0};
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

  weather_routing::OrderDepartureOffsets(offsets);

  EXPECT_EQ(offsets, (std::vector<int>{0, -60, 60, -180, 180}));
}

TEST(DepartureScheduler, BoundsOnlyDepartureCandidateBatches) {
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true), 4);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(2, true), 2);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, false), 20);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(0, false), 1);
}

TEST(DepartureScheduler, AutomaticModeUsesCpuCapacityAndLeavesHeadroom) {
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 0, 20), 4);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 0, 8), 4);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 0, 4), 2);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 0, 2), 1);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 0, 1), 1);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 0, 0), 4);
}

TEST(DepartureScheduler, ExplicitModeHonoursRouteGlobalAndSafetyBounds) {
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 6, 20), 6);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(4, true, 8, 20), 4);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 12, 20), 12);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 99, 20), 12);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, false, 6, 20), 20);
}

TEST(DepartureScheduler,
     NativeAuthoritativeCandidatesUseDeterministicHostServiceLane) {
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 0, 20, true),
            1);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(20, true, 12, 20, true),
            1);
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(2, true, 2, 4, true),
            1);
  // The isolation flag is scoped to a departure-candidate batch and must not
  // throttle unrelated route calculations.
  EXPECT_EQ(weather_routing::EffectiveRouteWorkerLimit(7, false, 0, 20, true),
            7);
}
