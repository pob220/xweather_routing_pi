#include <gtest/gtest.h>

#include <limits>

#include "ChartSafetyPolicy.h"

TEST(ChartSafetyPolicy, PositiveMinimumEnablesDepthAtRequestedThreshold) {
  PlugInSegmentSafetyOptions options = {};

  weather_routing::ApplyMinimumDepthPolicy(options, 5.0);

  EXPECT_EQ(options.check_depth, 1);
  EXPECT_DOUBLE_EQ(options.minimum_depth_m, 5.0);
}

TEST(ChartSafetyPolicy, ZeroNegativeAndInvalidMinimumDisableDepth) {
  const double values[] = {
      0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()};

  for (double value : values) {
    PlugInSegmentSafetyOptions options = {};
    options.check_depth = 1;
    options.minimum_depth_m = 99.0;

    weather_routing::ApplyMinimumDepthPolicy(options, value);

    EXPECT_EQ(options.check_depth, 0);
    EXPECT_DOUBLE_EQ(options.minimum_depth_m, 0.0);
  }
}
