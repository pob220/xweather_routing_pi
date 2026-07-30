/***************************************************************************
 * Compatibility wrapper for the vendored deterministic resource policy.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_RESOURCE_POLICY_H
#define WEATHER_ROUTING_RESOURCE_POLICY_H

#include <algorithm>

#include "supercpn/weather_routing/ResourcePolicy.h"

namespace weather_routing {

constexpr int kDefaultRoutingEffortPercent =
    static_cast<int>(
        supercpn::weather_routing::kDefaultRoutingEffortPercent);
constexpr int kMinimumRoutingEffortPercent =
    static_cast<int>(
        supercpn::weather_routing::kMinimumRoutingEffortPercent);
constexpr int kMaximumRoutingEffortPercent =
    static_cast<int>(
        supercpn::weather_routing::kMaximumRoutingEffortPercent);

inline int NormalizeRoutingEffortPercent(int percent) {
  return static_cast<int>(
      supercpn::weather_routing::normalizeRoutingEffortPercent(
          static_cast<unsigned>(std::max(0, percent))));
}

struct RoutingResourcePolicy {
  std::uint64_t maximum_generated_states{};
  std::uint64_t maximum_forward_generated_states{};
  std::uint64_t maximum_reverse_candidates{};
  std::uint64_t maximum_reverse_bridge_attempts{};
  std::uint64_t maximum_frontier_recovery_generated_states{};
  std::uint64_t maximum_frontier_recovery_labels{};
  std::uint64_t maximum_graph_generated_states{};
  std::uint64_t maximum_retained_states{};
  std::uint64_t maximum_graph_labels{};
};

inline std::uint64_t ScaleRoutingResource(std::uint64_t base, double scale,
                                          int effort_percent) {
  return supercpn::weather_routing::scaleRoutingResource(
      base, scale, static_cast<unsigned>(std::max(0, effort_percent)));
}

inline RoutingResourcePolicy SelectRoutingResourcePolicy(
    double route_distance_nm, int effort_percent, bool scout_preview) {
  const auto policy =
      supercpn::weather_routing::selectRoutingResourcePolicy(
          route_distance_nm,
          static_cast<unsigned>(std::max(0, effort_percent)), scout_preview);
  return {policy.maximumGeneratedStates,
          policy.maximumForwardGeneratedStates,
          policy.maximumReverseCandidates,
          policy.maximumReverseBridgeAttempts,
          policy.maximumFrontierRecoveryGeneratedStates,
          policy.maximumFrontierRecoveryLabels,
          policy.maximumGraphGeneratedStates,
          policy.maximumRetainedStates,
          policy.maximumGraphLabels};
}

}  // namespace weather_routing

#endif
