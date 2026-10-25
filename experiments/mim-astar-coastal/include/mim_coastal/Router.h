// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "supercpn/weather_routing/Providers.h"
#include "supercpn/weather_routing/Types.h"

namespace mim_coastal {
namespace wr = supercpn::weather_routing;

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

}  // namespace defaults

// Aggregate configuration: Router::route validates ranges before searching.
// Distances are nautical miles, headings are degrees, speeds are knots.
struct Options {
  // One persistent search: ocean fronts hand their actual histories to coastal
  // A*.
  bool coastEverywhere{false};  // Controlled A*-only ablation.
  bool continueAfterFirstRoute{
      false};  // Otherwise return first validated completion.
  bool guidanceEnabled{true};
  // Weighted best-first scheduling reduces nearly tied coastal branches.
  // Neither this score nor the coarse field is used as an acceptance bound.
  double guidanceWeight{1.0};
  double coastalBandNm{4.0};
  wr::Duration coastalStep{900};
  double coastalHeadingStepDegrees{5.0};
  // A rough land-distance field ranks work only; it is never an acceptance test
  // or an admissible bound. Missing/capped guidance falls back to direct
  // distance.
  unsigned guidanceGridCells{768};
  std::uint64_t maximumGuidanceQueries{6000};
  unsigned maximumHintCacheCells{4096};
  unsigned deferredServicePeriod{16};
  // Partial coastal expansion: try the best-looking headings first, retain
  // unfinished fans as cheap work records instead of materialising every child.
  unsigned coastalFanBatch{8};
  std::shared_ptr<const wr::LandAndBoundaryProvider> geographyGuidance;
  // MIM activates two proposals per sector/family; the remainder are deferred.
  // Range 8–360.
  unsigned sectors{defaults::sectors};
  // Primary coastal histories per cell, including expanded ones; excess choices
  // are deferred, not erased.
  unsigned labelsPerCell{2};
  double cellSizeNm{defaults::cellSizeNm};  // Valid range 0.01–100 NM.
  double headingStepDegrees{
      defaults::headingStepDegrees};  // Control fan: 1–90°.
  double headingBucketDegrees{
      defaults::headingBucketDegrees};  // A* bins: 1–90°.
  // Rough speed for work ordering, coastal lookahead and connection scheduling.
  double guidanceSpeedKnots{defaults::guidanceSpeedKnots};
  // Caller contract: bounds ALL permitted ground velocities for this request,
  // including every performance provider, current and propulsion mode. Zero
  // disables certified cost-bound pruning; geographical work ordering remains.
  // Observed violations abort but cannot prove a bound for unsampled weather.
  double certifiedMaximumGroundSpeedKnots{};
  // Disable approximate coastal cell competition (for reference comparisons).
  bool exactStateKeys{false};
  // Positive work allowances. Request limits may further restrict these.
  std::uint64_t maximumMotionAttempts{defaults::maximumMotionAttempts};
  std::uint64_t maximumConnectionAttempts{defaults::maximumConnectionAttempts};
  std::uint64_t maximumLabels{defaults::maximumLabels};
  std::uint64_t maximumWeatherSamples{defaults::maximumWeatherSamples};
  // Covers PMR search containers only, not the process or provider allocations.
  std::size_t searchMemoryBytes{defaults::searchMemoryBytes};
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
  std::uint64_t cellDeferred{};
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
  bool approximateSearch{};
  bool resourceLimited{};
  bool astarQueueExhausted{};
  std::uint64_t coastalTransitions{};
  std::uint64_t blockedOceanEscalations{};
  std::uint64_t deferredLabels{};
  std::uint64_t deferredResumed{};
  std::uint64_t hintQueries{};
  std::uint64_t hintUnavailable{};
  std::uint64_t hintCacheHits{};
  std::uint64_t hintCacheCells{};
  std::uint64_t guidanceCells{};
  double guidanceBuildSeconds{};
  std::string stopReason;
};

struct Result {
  wr::RoutingResult route;
  Diagnostics search;
};

// Stateless and reentrant. All search state belongs to one route() call.
// Providers remain caller-owned and must support the caller's concurrency
// model. Only deterministic fastest-route objectives and latitudes within ±85°
// are supported. No route is delivered without chronological replay. Ordinary
// work exhaustion may preserve an incumbent; cancellation or failed authority
// cannot.
class Router {
public:
  [[nodiscard]] Result route(const wr::RoutingRequest& request,
                             const wr::RoutingEnvironment& environment,
                             const Options& options = {}) const;
};
}  // namespace mim_coastal
