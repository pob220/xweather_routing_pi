// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory_resource>
#include <new>
#include <queue>
#include <string>
#include <tuple>
#include <vector>

#include "MotionKernel.h"
#include "mim_astar/Router.h"

namespace mim_astar::detail {
namespace motion = wr::internal;
inline constexpr auto noParent = std::numeric_limits<std::size_t>::max();
inline constexpr double nauticalMilesPerLatitudeDegree = 60.0;
inline constexpr double minimumLongitudeScale = 0.01;
inline constexpr double degreesPerHalfTurn = 180.0;

// Accounts search containers, not providers, result legs or strings owned by
// the shared physical model. The independent label/work limits remain active.
class BudgetMemoryResource final : public std::pmr::memory_resource {
public:
  explicit BudgetMemoryResource(std::size_t limit) : limit_(limit) {}
  // Allocation counters belong to exactly one resource identity. Copying them
  // while containers still refer to the old resource would corrupt accounting.
  BudgetMemoryResource(const BudgetMemoryResource&) = delete;
  BudgetMemoryResource& operator=(const BudgetMemoryResource&) = delete;
  BudgetMemoryResource(BudgetMemoryResource&&) = delete;
  BudgetMemoryResource& operator=(BudgetMemoryResource&&) = delete;
  ~BudgetMemoryResource() override = default;
  [[nodiscard]] std::size_t peak() const { return peak_; }

private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > limit_ - used_) throw std::bad_alloc{};
    auto* allocation = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    used_ += bytes;
    peak_ = std::max(peak_, used_);
    return allocation;
  }
  void do_deallocate(void* allocation, std::size_t bytes,
                     std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(allocation, bytes, alignment);
    used_ -= bytes;
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
  std::size_t limit_;
  std::size_t used_{};
  std::size_t peak_{};
};

struct SearchNode {
  motion::MotionState state;
  wr::RouteLeg incoming;
  std::size_t parent{noParent};
  bool active{true};
};

// Time is exact. Every node retains full resource history. Approximate mode
// bins position/heading and permits a bounded competition between resource
// histories; exact mode distinguishes the entire physical continuation state.
// Coordinates/headings hold exact values or integer-valued bins according to
// Options::exactStateKeys. Field order also defines deterministic map ordering.
struct StateKey {
  double northCoordinate;
  double eastCoordinate;
  std::int64_t utcSeconds;
  double incomingHeading;
  wr::Tack tack;
  wr::PropulsionMode mode;
  wr::ProfileRole role;
  std::string profileIdentity;
  int sailPlan;
  std::int64_t modeDurationOrRemainingDwell;
  std::int64_t motorSeconds;
  std::int64_t waitingSeconds;
  bool departureEgressActive;
  double fuel;
  double risk;
  unsigned manoeuvres;
  auto operator<=>(const StateKey&) const = default;
};

class StatePartition {
public:
  StatePartition(const wr::RoutingRequest& request, const Options& options)
      : origin_(request.start),
        exactStateKeys_(options.exactStateKeys),
        cellSizeNm_(options.cellSizeNm),
        headingBucketDegrees_(options.headingBucketDegrees),
        longitudeScale_(
            nauticalMilesPerLatitudeDegree *
            std::max(minimumLongitudeScale,
                     std::cos(origin_.latitude * std::acos(-1.0) / degreesPerHalfTurn))),
        minimumMotorRun_(request.vessel.propulsion.minimumMotorRun.count()) {}

  [[nodiscard]] StateKey key(const motion::MotionState& state) const {
    double north = state.position.latitude;
    double east = state.position.longitude;
    double heading = wr::normalizeHeading(state.incomingHeading);
    if (!exactStateKeys_) {
      north = std::floor((north - origin_.latitude) * nauticalMilesPerLatitudeDegree /
                         cellSizeNm_);
      east = std::floor(wr::normalizeLongitude(east - origin_.longitude) *
                        longitudeScale_ / cellSizeNm_);
      heading = std::floor(heading / headingBucketDegrees_);
    }
    const auto remainingDwell =
        state.mode == wr::PropulsionMode::Sail
            ? 0
            : std::max<std::int64_t>(0, minimumMotorRun_ - state.modeDuration.count());
    return {north,
            east,
            state.time.time_since_epoch().count(),
            heading,
            state.tack,
            state.mode,
            state.role,
            state.profileIdentity,
            state.sailPlan,
            exactStateKeys_ ? state.modeDuration.count() : remainingDwell,
            exactStateKeys_ ? state.motorDuration.count() : 0,
            exactStateKeys_ ? state.waitDuration.count() : 0,
            state.departureEgressActive,
            exactStateKeys_ ? state.fuel : 0,
            exactStateKeys_ ? state.risk : 0,
            exactStateKeys_ ? state.manoeuvres : 0};
  }

private:
  wr::GeoPoint origin_;
  bool exactStateKeys_;
  double cellSizeNm_;
  double headingBucketDegrees_;
  double longitudeScale_;
  std::int64_t minimumMotorRun_{};
};

struct QueueEntry {
  double lowerBoundSeconds{};
  double remainingNm{};
  std::size_t node{};
  bool operator>(const QueueEntry& other) const {
    return std::tie(lowerBoundSeconds, remainingNm, node) >
           std::tie(other.lowerBoundSeconds, other.remainingNm, other.node);
  }
};

struct SearchStorage {
  explicit SearchStorage(BudgetMemoryResource& memory)
      : nodes(&memory),
        cells(&memory),
        queue(std::greater<QueueEntry>{}, std::pmr::vector<QueueEntry>{&memory}) {}
  std::pmr::deque<SearchNode> nodes;
  std::pmr::map<StateKey, std::pmr::vector<std::size_t>> cells;
  std::priority_queue<QueueEntry, std::pmr::vector<QueueEntry>, std::greater<QueueEntry>>
      queue;
};
}  // namespace mim_astar::detail
