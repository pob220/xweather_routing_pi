// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "PassageWeather.h"
#include "BundledClimatology.h"
#include <wx/datetime.h>
#include <wx/filefn.h>
#include <thread>
#include <cmath>

namespace {
const wxString source = wxString::FromUTF8(WEATHER_ROUTING_SOURCE_DIR);
wxDateTime Date(const char* s) {
  wxDateTime t;
  t.ParseISOCombined(s);
  return t;
}
TEST(PassageWeather, LoadsSurfaceWindWithoutAnotherPlugin) {
  weather_routing::PassageWeather w;
  wxString error;
  ASSERT_TRUE(w.Open(source + "/ci/testdata/irish-sea-uniform-wind.grb", error))
      << error;
  EXPECT_LE(w.FirstWindTime(), w.LastWindTime());
  EXPECT_TRUE(w.HasWind(w.FirstWindTime(), 53.4, -5.5));
  EXPECT_FALSE(w.HasWind(w.FirstWindTime(), -50, 100));
  EXPECT_FALSE(w.HasWind(w.FirstWindTime() - 1, 53.4, -5.5));
  EXPECT_FALSE(w.HasWind(w.LastWindTime() + 1, 53.4, -5.5));
}
TEST(PassageWeather, FailedReplacementKeepsSelectedForecast) {
  weather_routing::PassageWeather w;
  wxString error;
  ASSERT_TRUE(
      w.Open(source + "/ci/testdata/irish-sea-uniform-wind.grb", error));
  const auto path = w.Path();
  EXPECT_FALSE(w.Open(source + "/data/boats/Boat.xml", error));
  EXPECT_EQ(w.Path(), path);
  EXPECT_TRUE(w.HasWind(w.FirstWindTime(), 53.4, -5.5));
}
TEST(PassageWeather, DeliveredFrameOwnsItsData) {
  std::unique_ptr<GribRecordSet> frame;
  {
    weather_routing::PassageWeather w;
    wxString error;
    ASSERT_TRUE(
        w.Open(source + "/ci/testdata/irish-sea-uniform-wind.grb", error));
    frame = w.Frame(w.FirstWindTime());
  }
  ASSERT_NE(frame->m_GribRecordPtrArray[Idx_WIND_VX], nullptr);
  EXPECT_TRUE(std::isfinite(
      frame->m_GribRecordPtrArray[Idx_WIND_VX]->getInterpolatedValue(-5.5,
                                                                     53.4)));
}
TEST(BundledClimatology, WorksOfflineAndPreservesMissingMasks) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  int count = 8;
  double f[8]{}, s[8]{}, gale, calm;
  EXPECT_TRUE(
      C::Atlas(Date("2026-11-07T08:00:00"), 30, -40, count, f, s, gale, calm));
  double sum = 0;
  for (int i = 0; i < 8; ++i) {
    EXPECT_GE(f[i], 0);
    EXPECT_GE(s[i], 0);
    sum += f[i];
  }
  EXPECT_NEAR(sum, 1, 0.08);
  EXPECT_GE(gale, 0);
  EXPECT_LE(gale, 1);
  EXPECT_GE(calm, 0);
  EXPECT_LE(calm, 1);
  double dir, speed;
  EXPECT_TRUE(C::Data(1, Date("2026-11-07T08:00:00"), 30, -40, dir, speed));
  EXPECT_GE(speed, 0);
  EXPECT_LT(dir, 360);
  EXPECT_FALSE(
      C::Atlas(Date("2026-11-07T08:00:00"), 0, 20, count, f, s, gale, calm));
  EXPECT_FALSE(C::Data(1, Date("2026-11-07T08:00:00"), 89, -40, dir, speed));
}
TEST(BundledClimatology, DatasetVersionTakesPrecedenceOverPluginVersion) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  const auto bundled = C::Resolve();
  EXPECT_NE(bundled.data, nullptr);
  EXPECT_NE(bundled.wind_atlas, nullptr);
  auto external = bundled;
  external.major = 20;
  external.dataset = "ocpn-climatology-2026.1";
  EXPECT_EQ(C::Resolve(external).dataset, "ocpn-climatology-2026.2");
  external.dataset = "ocpn-climatology-2026.3";
  EXPECT_EQ(C::Resolve(external).dataset, external.dataset);
  external.valid = false;
  EXPECT_EQ(C::Resolve(external).dataset, "ocpn-climatology-2026.2");
}
TEST(BundledClimatology, WorkerQueriesAndLongitudeWrapAreConsistent) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  double da, sa, db, sb;
  ASSERT_TRUE(C::Data(0, Date("2026-01-01T00:00:00"), 30, -40, da, sa));
  bool ok = false;
  std::thread worker(
      [&] { ok = C::Data(0, Date("2026-01-01T00:00:00"), 30, 320, db, sb); });
  worker.join();
  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(da, db);
  EXPECT_DOUBLE_EQ(sa, sb);
}
TEST(BundledClimatology, CycloneIndexIsAvailableWithoutPlugin) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  EXPECT_EQ(C::Cyclones(50, -5, 50.1, -5.1, Date("2026-11-07T08:00:00"), 365),
            0);
}
}  // namespace
