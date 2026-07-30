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
  std::uint64_t maximum_forward_generated_states;
  std::uint64_t maximum_reverse_candidates;
  std::uint64_t maximum_reverse_bridge_attempts;
  std::uint64_t maximum_frontier_recovery_generated_states;
  std::uint64_t maximum_frontier_recovery_labels;
  std::uint64_t maximum_graph_generated_states;
  std::uint64_t maximum_retained_states;
  std::uint64_t maximum_graph_labels;
};

inline std::uint64_t ScaleRoutingResource(std::uint64_t base, double scale,
                                          int effort_percent) {
  // Quantise the 100% policy first, then scale that integer baseline. This is
  // deliberate: the engine can reconstruct the exact 100/150/200 tiers from a
  // 400% request, so selecting a higher effort truly replays the same lower
  // tier before adding work.
  const long double baselineValue =
      static_cast<long double>(base) * static_cast<long double>(scale);
  const long double maximum = std::numeric_limits<std::uint64_t>::max();
  if (baselineValue >= maximum)
    return std::numeric_limits<std::uint64_t>::max();
  const std::uint64_t baseline =
      static_cast<std::uint64_t>(std::floor(baselineValue + 0.5L));
  const long double value =
      static_cast<long double>(baseline) *
      static_cast<long double>(NormalizeRoutingEffortPercent(effort_percent)) /
      100.0L;
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
    const std::uint64_t forward =
        ScaleRoutingResource(60000, distance_scale, 100);
    return {forward, forward, 0, 0, 0, 0, 0,
            ScaleRoutingResource(10000, distance_scale, 100), 1};
  }

  // Keep the established forward/global proportions, but give reverse bridge
  // integration and the new frontier-recovery phase independent allowances.
  // An earlier stage must not consume a later fallback's reserve simply
  // because it runs first.
  const std::uint64_t forward =
      ScaleRoutingResource(675000, distance_scale, effort_percent);
  const std::uint64_t frontier =
      ScaleRoutingResource(225000, distance_scale, effort_percent);
  const std::uint64_t graph =
      ScaleRoutingResource(225000, distance_scale, effort_percent);
  return {
      forward + frontier + graph,
      forward,
      ScaleRoutingResource(512, distance_scale, effort_percent),
      ScaleRoutingResource(40960, distance_scale, effort_percent),
      frontier,
      ScaleRoutingResource(90000, distance_scale, effort_percent),
      graph,
      ScaleRoutingResource(70000, distance_scale, effort_percent),
      ScaleRoutingResource(180000, distance_scale, effort_percent)};
}

}  // namespace weather_routing

#endif
