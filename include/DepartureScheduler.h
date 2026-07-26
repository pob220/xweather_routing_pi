/***************************************************************************
 * Deterministic, bounded scheduling policy for departure-time candidates.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_DEPARTURE_SCHEDULER_H
#define WEATHER_ROUTING_DEPARTURE_SCHEDULER_H

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace weather_routing {

constexpr int kMaximumParallelDepartureCandidates = 4;

inline void OrderDepartureOffsets(std::vector<int>& offsets) {
  std::sort(offsets.begin(), offsets.end(), [](int left, int right) {
    const int left_distance = std::abs(left);
    const int right_distance = std::abs(right);
    if (left_distance != right_distance) return left_distance < right_distance;
    return left < right;
  });
}

inline int EffectiveRouteWorkerLimit(int configured_limit,
                                     bool departure_candidates_active) {
  const int valid_limit = std::max(1, configured_limit);
  if (!departure_candidates_active) return valid_limit;
  return std::min(valid_limit, kMaximumParallelDepartureCandidates);
}

}  // namespace weather_routing

#endif
