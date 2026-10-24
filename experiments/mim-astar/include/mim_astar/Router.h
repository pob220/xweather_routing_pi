// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "supercpn/weather_routing/Providers.h"
#include "supercpn/weather_routing/Types.h"

namespace mim_astar {
namespace wr = supercpn::weather_routing;

enum class Strategy : std::uint8_t { MimOnly, AStarOnly, MimThenAStar };

// Research defaults, not values selected by a production resource policy.
namespace defaults {
inline constexpr unsigned sectors = 24;
inline constexpr double cellSizeNm = 0.5;
inline constexpr double headingStepDegrees = 10.0;
inline constexpr double headingBucketDegrees = 30.0;
inline constexpr double guidanceSpeedKnots = 6.0;
inline constexpr std::uint64_t maximumMotionAttempts = 120'000;
inline constexpr std::uint64_t maximumConnectionAttempts = 10'000;
inline constexpr std::uint64_t maximumLabels = 40'000;
inline constexpr std::uint64_t maximumWeatherSamples = 4'000'000;
inline constexpr std::size_t searchMemoryBytes = 64ULL * 1024 * 1024;
inline constexpr double mimWorkFraction = 0.5;
inline constexpr unsigned maximumImprovementExpansions = 512;
}  // namespace defaults

// Aggregate configuration: Router::route validates ranges before searching.
// Distances are nautical miles, headings are degrees, speeds are knots.
struct Options {
  Strategy strategy{Strategy::MimThenAStar};
  // MIM retains two proposals per sector and physical family; valid range 8–360.
  unsigned sectors{defaults::sectors};
  // A* approximate cell quota (1–32). This is not exact Pareto dominance.
  unsigned labelsPerCell{2};
  double cellSizeNm{defaults::cellSizeNm};                  // Valid range 0.01–100 NM.
  double headingStepDegrees{defaults::headingStepDegrees};  // Control fan: 1–90°.
  double headingBucketDegrees{defaults::headingBucketDegrees};  // A* bins: 1–90°.
  // Used only to order MIM candidates and schedule connection attempts.
  double guidanceSpeedKnots{defaults::guidanceSpeedKnots};
  // Caller contract: bounds ALL permitted ground velocities for this request,
  // including every performance provider, current and propulsion mode. Zero
  // uses Dijkstra ordering. Observed violations abort; that check cannot prove
  // a bound for unsampled weather. This does not undo approximate cell pruning.
  double certifiedMaximumGroundSpeedKnots{};
  // Distinguishes full continuation state in A*. MIM still prunes by sector.
  bool exactStateKeys{false};
  // Refine motion steps near arrival/land, without changing integration physics.
  bool adaptiveSteps{true};
  // Positive work allowances. Request limits may further restrict these.
  std::uint64_t maximumMotionAttempts{defaults::maximumMotionAttempts};
  std::uint64_t maximumConnectionAttempts{defaults::maximumConnectionAttempts};
  std::uint64_t maximumLabels{defaults::maximumLabels};
  std::uint64_t maximumWeatherSamples{defaults::maximumWeatherSamples};
  // Covers PMR search containers only, not the process or provider allocations.
  std::size_t searchMemoryBytes{defaults::searchMemoryBytes};
  // Hybrid stage split, strictly between 0 and 1; ignored by single-stage modes.
  double mimWorkFraction{defaults::mimWorkFraction};
  // Additional A* expansions after its first incumbent, including a MIM seed.
  unsigned maximumImprovementExpansions{defaults::maximumImprovementExpansions};
  // Synchronous borrowed view of a validated incumbent, valid during the call.
  // Copy the result to retain it. Observer exceptions are counted and isolated.
  std::function<void(const wr::RoutingResult&)> onIncumbent;
};

struct Diagnostics {
  std::uint64_t motionAttempts{};
  std::uint64_t connectionAttempts{};
  std::uint64_t expandedLabels{};
  std::uint64_t mimMotionAttempts{};
  std::uint64_t astarMotionAttempts{};
  std::uint64_t mimLayers{};
  std::uint64_t retainedLabels{};
  std::uint64_t cellPruned{};
  std::uint64_t boundPruned{};
  std::uint64_t staleQueueEntries{};
  std::uint64_t validatedCandidates{};
  std::uint64_t rejectedCandidates{};
  std::uint64_t improvements{};
  std::uint64_t waitCandidates{};
  std::uint64_t observerErrors{};
  std::size_t peakManagedBytes{};
  std::size_t peakQueueEntries{};
  // Wall time until first acceptance; zero also occurs when no route is found.
  double firstIncumbentSeconds{};
  bool usesCertifiedBound{};
  bool approximatePruning{};
  bool resourceLimited{};
  bool astarQueueExhausted{};
  std::string mimYieldReason;
  std::string stopReason;
};

struct Result {
  wr::RoutingResult route;
  Diagnostics search;
};

// Stateless and reentrant. All search state belongs to one route() call.
// Providers remain caller-owned and must support the caller's concurrency model.
// Only deterministic fastest-route objectives and latitudes within ±85° are
// supported. No route is delivered without chronological replay. Ordinary work
// exhaustion may preserve an incumbent; cancellation or failed authority cannot.
class Router {
public:
  [[nodiscard]] Result route(const wr::RoutingRequest& request,
                             const wr::RoutingEnvironment& environment,
                             const Options& options = {}) const;
};
}  // namespace mim_astar
