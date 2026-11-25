// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <wx/mstream.h>

#include "WorldGribModels.h"

using namespace weather_routing;

TEST(WorldGribModels, BundledCatalogueHasWeatherAndCurrentProducts) {
  const auto models = ReadWorldGribModels(
      wxString::FromUTF8(WEATHER_ROUTING_SOURCE_DIR) + "/data/world-models.json");
  ASSERT_EQ(models.size(), 11U);
  int currentCount = 0;
  for (const auto& model : models) {
    if (IsWorldCurrentModel(model)) ++currentCount;
  }
  EXPECT_EQ(currentCount, 3);
}

TEST(WorldGribModels, RejectsDuplicateIdsAndInvalidLimits) {
  const auto check = [](const std::string& data) {
    wxMemoryInputStream stream(data.data(), data.size());
    return ParseWorldGribModels(stream);
  };
  EXPECT_TRUE(check(R"([{"name":"x","title":"X","maxlength":0}])").empty());
  EXPECT_TRUE(check(R"([{"name":"x","title":"X","maxlength":1},
                        {"name":"x","title":"Y","maxlength":2}])").empty());
  EXPECT_TRUE(check(R"([{"name":"x&wrong","title":"X","maxlength":1}])").empty());
}

TEST(WorldGribModels, UsesExactHoursAndCrossingAreaInCompressedRequest) {
  const WorldGribModel model{"ecmwf0p1", "ECMWF IFS", 360};
  EXPECT_EQ(WorldGribUrl(model, 137, -3, 4, 175, -175),
      "https://grib.bosun.io/grib?model=ecmwf0p1&latmin=-3&latmax=4"
      "&lonmin=175&lonmax=-175&length=137&encoding=ccsds");
  EXPECT_THROW(WorldGribUrl(model, 361, -3, 4, 175, -175),
               std::invalid_argument);
}
