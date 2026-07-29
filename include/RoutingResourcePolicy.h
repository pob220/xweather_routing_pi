/***************************************************************************
 * Deterministic resource policy for final routes and chart-safety scouts.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_RESOURCE_POLICY_H
#define WEATHER_ROUTING_RESOURCE_POLICY_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace weather_routing {

constexpr int kDefaultRoutingEffortPercent = 100;
constexpr int kMinimumRoutingEffortPercent = 100;
constexpr int kMaximumRoutingEffortPercent = 400;

inline int NormalizeRoutingEffortPercent(int percent) {
  // Persist only supported policy levels.  Mapping hand-edited or older
  // configuration values to the nearest level keeps the UI and engine in
  // agreement instead of silently running an unrepresentable percentage.
  if (percent <= 125) return 100;
  if (percent <= 175) return 150;
  if (percent <= 300) return 200;
  return 400;
}

struct RoutingResourcePolicy {
  std::uint64_t maximum_generated_states;
  std::uint64_t maximum_retained_states;
  std::uint64_t maximum_graph_labels;
};

inline std::uint64_t ScaleRoutingResource(std::uint64_t base, double scale,
                                          int effort_percent) {
  const long double value =
      static_cast<long double>(base) * static_cast<long double>(scale) *
      static_cast<long double>(NormalizeRoutingEffortPercent(effort_percent)) /
      100.0L;
  const long double maximum = std::numeric_limits<std::uint64_t>::max();
  if (value >= maximum) return std::numeric_limits<std::uint64_t>::max();
  return static_cast<std::uint64_t>(std::floor(value + 0.5L));
}

inline RoutingResourcePolicy SelectRoutingResourcePolicy(
    double route_distance_nm, int effort_percent, bool scout_preview) {
  const double finite_distance =
      std::isfinite(route_distance_nm) ? route_distance_nm : 100.0;
  const double distance_scale =
      std::clamp(finite_distance / 100.0, 0.6, 4.0);

  if (scout_preview) {
    // A scout is a deterministic cache-preparation hint.  Its work must not
    // depend on CPU load or cache warmth.  The 60k base reproduces roughly the
    // useful work formerly completed by the five-second timeout while keeping
    // recovery and graph exploration out of the preview.
    return {ScaleRoutingResource(60000, distance_scale, 100),
            ScaleRoutingResource(10000, distance_scale, 100), 1};
  }

  return {ScaleRoutingResource(900000, distance_scale, effort_percent),
          ScaleRoutingResource(70000, distance_scale, effort_percent),
          ScaleRoutingResource(180000, distance_scale, effort_percent)};
}

}  // namespace weather_routing

#endif
