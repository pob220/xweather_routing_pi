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
