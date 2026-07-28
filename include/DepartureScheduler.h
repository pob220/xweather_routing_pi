/***************************************************************************
 * Deterministic, bounded scheduling policy for departure-time candidates.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_DEPARTURE_SCHEDULER_H
#define WEATHER_ROUTING_DEPARTURE_SCHEDULER_H

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace weather_routing {

// Zero is stored by the route configuration to mean that the scheduler should
// choose a conservative machine-appropriate value.
constexpr int kAutomaticParallelDepartureCandidates = 0;
constexpr int kMaximumParallelDepartureCandidates = 12;
constexpr int kMaximumAutomaticParallelDepartureCandidates = 4;
constexpr int kFallbackAutomaticParallelDepartureCandidates = 4;

inline void OrderDepartureOffsets(std::vector<int>& offsets) {
  std::sort(offsets.begin(), offsets.end(), [](int left, int right) {
    const int left_distance = std::abs(left);
    const int right_distance = std::abs(right);
    if (left_distance != right_distance) return left_distance < right_distance;
    return left < right;
  });
}

inline int AutomaticDepartureWorkerLimit(int logical_cpu_count) {
  if (logical_cpu_count <= 0)
    return kFallbackAutomaticParallelDepartureCandidates;

  // Leave two logical CPUs available for OpenCPN's UI, chart services and the
  // operating system.  Four concurrent route engines is a deliberately
  // conservative automatic upper bound because each route can retain a large
  // search working set. Users may explicitly select a higher tested value.
  return std::max(
      1, std::min(kMaximumAutomaticParallelDepartureCandidates,
                  logical_cpu_count - 2));
}

inline int EffectiveRouteWorkerLimit(
    int configured_limit, bool departure_candidates_active,
    int requested_departure_workers = kAutomaticParallelDepartureCandidates,
    int logical_cpu_count = 0) {
  const int valid_limit = std::max(1, configured_limit);
  if (!departure_candidates_active) return valid_limit;

  const int departure_limit =
      requested_departure_workers <= kAutomaticParallelDepartureCandidates
          ? AutomaticDepartureWorkerLimit(logical_cpu_count)
          : std::min(requested_departure_workers,
                     kMaximumParallelDepartureCandidates);
  return std::min(valid_limit, departure_limit);
}

}  // namespace weather_routing

#endif
