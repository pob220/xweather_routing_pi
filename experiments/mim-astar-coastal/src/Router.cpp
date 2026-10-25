// SPDX-License-Identifier: GPL-3.0-or-later
#include "mim_coastal/Router.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "MotionKernel.h"
#include "SearchStorage.h"
#include "GeographyHints.h"
#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/Providers.h"
#include "supercpn/weather_routing/Types.h"

namespace mim_coastal {
namespace {
namespace motion = wr::internal;
using Clock = std::chrono::steady_clock;

constexpr double secondsPerHour = 3600.0;
constexpr double fullTurnDegrees = 360.0;
constexpr double halfTurnDegrees = 180.0;
constexpr double quarterTurnDegrees = 90.0;
constexpr double maximumLatitudeDegrees = 85.0;
constexpr unsigned minimumSectorCount = 8;
constexpr unsigned maximumLabelsPerCell = 32;
constexpr double minimumCellSizeNm = 0.01;
constexpr double maximumCellSizeNm = 100.0;
constexpr std::size_t minimumSearchMemoryBytes = 1024;
constexpr double headingEnvelopeSlackDegrees = 1e-9;
constexpr double duplicateHeadingToleranceDegrees = 1e-8;
constexpr double speedBoundComparisonSlackKnotSeconds = 1e-5;
// These match the existing checked-connection/validator numerical contract.
constexpr double numericalEndpointAllowanceNm = 0.15;
constexpr double numericalArrivalSlackSeconds = 1.0;
constexpr wr::Duration maximumIntegrationSlice{300};
constexpr wr::Duration minimumMovingTimeAfterManoeuvre{60};
constexpr wr::Duration minimumMimArrivalStep{600};
constexpr wr::Duration minimumConnectionWindow{7200};

// Errors abort the current search stack. The expected MIM-to-A* transition uses
// an ordinary return so exception handling is reserved for failures (E.3).
struct StopSearch final : std::exception {
  StopSearch(wr::RoutingStatus failureStatus, std::string failureReason)
      : status(failureStatus), reason(std::move(failureReason)) {}
  [[nodiscard]] const char* what() const noexcept override {
    return reason.c_str();
  }
  wr::RoutingStatus status;
  std::string reason;
};

bool validPoint(wr::GeoPoint point) {
  return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
         std::abs(point.latitude) <= maximumLatitudeDegrees &&
         std::abs(point.longitude) <= halfTurnDegrees;
}

bool validOptions(const Options& options) {
  return options.coastalFanBatch > 0 && options.coastalFanBatch <= 360 &&
         std::isfinite(options.guidanceWeight) && options.guidanceWeight >= 1 &&
         options.guidanceWeight <= 4 && options.sectors >= minimumSectorCount &&
         options.sectors <= 360 && options.labelsPerCell > 0 &&
         options.labelsPerCell <= maximumLabelsPerCell &&
         std::isfinite(options.cellSizeNm) &&
         options.cellSizeNm >= minimumCellSizeNm &&
         options.cellSizeNm <= maximumCellSizeNm &&
         std::isfinite(options.headingStepDegrees) &&
         options.headingStepDegrees >= 1 && options.headingStepDegrees <= 90 &&
         std::isfinite(options.coastalHeadingStepDegrees) &&
         options.coastalHeadingStepDegrees >= 1 &&
         options.coastalHeadingStepDegrees <= 90 &&
         std::isfinite(options.headingBucketDegrees) &&
         options.headingBucketDegrees >= 1 &&
         options.headingBucketDegrees <= 90 &&
         std::isfinite(options.guidanceSpeedKnots) &&
         options.guidanceSpeedKnots > 0 &&
         std::isfinite(options.certifiedMaximumGroundSpeedKnots) &&
         options.certifiedMaximumGroundSpeedKnots >= 0 &&
         std::isfinite(options.coastalBandNm) && options.coastalBandNm >= 0 &&
         options.coastalStep > wr::Duration::zero() &&
         options.guidanceGridCells >= 16 &&
         options.guidanceGridCells <= 16384 &&
         options.maximumHintCacheCells > 0 &&
         options.maximumHintCacheCells <= 65536 &&
         options.maximumGuidanceQueries > 0 &&
         options.maximumMotionAttempts > 0 &&
         options.maximumConnectionAttempts > 0 && options.maximumLabels > 0 &&
         options.maximumWeatherSamples > 0 &&
         options.searchMemoryBytes >= minimumSearchMemoryBytes;
}

// One request owns physics references, budgets and the validated incumbent.
// A single history survives changes between ocean and coastal search.
class SearchSession {
public:
  SearchSession(const wr::RoutingRequest& request,
                const wr::RoutingEnvironment& environment,
                const Options& options,
                const wr::VesselPerformanceModel& performance, Result& result)
      : request_(request),
        environment_(environment),
        options_(options),
        performance_(performance),
        output_(result),
        partition_(request, options),
        memory_(options.searchMemoryBytes),
        started_(Clock::now()),
        geography_(request, environment, options, output_.search) {}

  void run() {
    try {
      runAdaptive();
      checkCancellation();
      if (!output_.route.validation.passed) {
        output_.route.status = dataFailure_ == wr::RoutingStatus::Complete
                                   ? wr::RoutingStatus::SearchIncomplete
                                   : dataFailure_;
        output_.route.message =
            "Persistent MIM/coastal A* found no validated route";
      }
      if (output_.search.stopReason.empty())
        output_.search.stopReason = "Retained search exhausted";
    } catch (const StopSearch& stop) {
      finishStopped(stop.status, stop.reason);
    } catch (const std::bad_alloc&) {
      finishStopped(wr::RoutingStatus::ResourceLimitReached,
                    "Managed search-memory allowance reached");
    }
    output_.search.peakManagedBytes = memory_.peak();
  }

private:
  void finishStopped(wr::RoutingStatus status, const std::string& reason) {
    output_.search.stopReason = reason;
    output_.search.resourceLimited =
        status == wr::RoutingStatus::ResourceLimitReached;
    if (output_.search.resourceLimited)
      output_.route.diagnostics.resourceLimitEvents.push_back(reason);
    // Explicit cancellation and a violated bound invalidate delivery, including
    // an earlier incumbent. Ordinary budget exhaustion can preserve one.
    if (status == wr::RoutingStatus::Cancelled ||
        status == wr::RoutingStatus::ValidationFailure ||
        status == wr::RoutingStatus::InvalidVesselConfiguration ||
        !output_.route.validation.passed) {
      output_.route.status = status;
      output_.route.message = reason;
      output_.route.legs.clear();
      output_.route.validation = {};
      output_.route.metrics = {};
    }
  }

  void checkCancellation() const {
    if (request_.cancellation.cancelled())
      throw StopSearch{wr::RoutingStatus::Cancelled,
                       "MIM/A* request cancelled"};
  }

  void checkBudget() const {
    checkCancellation();
    if (output_.search.motionAttempts >= options_.maximumMotionAttempts ||
        output_.route.diagnostics.generatedStates >=
            request_.limits.maximumGeneratedStates ||
        output_.route.diagnostics.weatherSamples >=
            options_.maximumWeatherSamples)
      throw StopSearch{wr::RoutingStatus::ResourceLimitReached,
                       "Motion, generated-state or weather allowance reached"};
  }

  [[nodiscard]] double elapsed(const motion::MotionState& state) const {
    return static_cast<double>((state.time - request_.departure).count());
  }

  [[nodiscard]] double remaining(const motion::MotionState& state) const {
    return wr::distanceNm(state.position, request_.destination);
  }

  [[nodiscard]] double lowerBound(const motion::MotionState& state) const {
    const double speed = options_.certifiedMaximumGroundSpeedKnots;
    // The shared validator admits a small numerical endpoint tolerance.
    // Removing it makes this bound conservative for that acceptance contract.
    const double distance =
        std::max(0.0, remaining(state) -
                          std::max(numericalEndpointAllowanceNm,
                                   request_.options.destinationToleranceNm));
    return speed > 0 ? std::max(0.0, distance * secondsPerHour / speed -
                                         numericalArrivalSlackSeconds)
                     : 0.0;
  }

  [[nodiscard]] double incumbentSeconds() const {
    return output_.route.validation.passed
               ? static_cast<double>(output_.route.metrics.elapsed.count())
               : std::numeric_limits<double>::infinity();
  }

  [[nodiscard]] bool admissibleState(const motion::MotionState& state) const {
    return validPoint(state.position) &&
           state.time <=
               request_.departure + request_.limits.maximumRouteDuration &&
           wr::distanceNm(request_.start, state.position) <=
               request_.limits.maximumExplorationDistanceNm &&
           elapsed(state) + lowerBound(state) < incumbentSeconds();
  }

  [[nodiscard]] motion::MotionState initialState() const {
    motion::MotionState initial;
    initial.position = request_.start;
    initial.time = request_.departure;
    if (environment_.landAndBoundaries &&
        request_.constraints.landSafetyMarginNm > 0)
      initial.departureEgressActive =
          environment_.landAndBoundaries->distanceToForbiddenNm(
              request_.start) < request_.constraints.landSafetyMarginNm;
    return initial;
  }

  std::size_t append(detail::SearchStorage& storage,
                     motion::MotionCandidate candidate, std::size_t parent) {
    const auto limit =
        std::min(options_.maximumLabels, request_.limits.maximumRetainedStates);
    if (storage.nodes.size() >= limit)
      throw StopSearch{wr::RoutingStatus::ResourceLimitReached,
                       "Retained lineage-label allowance reached"};
    storage.nodes.emplace_back(std::move(candidate.state),
                               std::move(candidate.leg), parent, &memory_);
    output_.search.retainedLabels = std::max<std::uint64_t>(
        output_.search.retainedLabels, storage.nodes.size());
    output_.route.diagnostics.retainedStates = output_.search.retainedLabels;
    return storage.nodes.size() - 1;
  }

  [[nodiscard]] std::vector<double> headings(const motion::MotionState& state,
                                             double separation) {
    const double destinationBearing =
        wr::initialBearingDegrees(state.position, request_.destination);
    std::vector<double> directions{destinationBearing};
    if (const auto guide = geography_.direction(state.position)) {
      if (std::abs(wr::angularDifferenceDegrees(*guide, destinationBearing)) <=
          request_.options.maximumSearchAngleDegrees +
              headingEnvelopeSlackDegrees)
        directions.insert(directions.begin(), *guide);
    }
    const unsigned count =
        static_cast<unsigned>(std::ceil(fullTurnDegrees / separation));
    for (unsigned index = 0; index < count; ++index) {
      const double heading = fullTurnDegrees * index / count;
      if (std::abs(wr::angularDifferenceDegrees(heading, destinationBearing)) <=
              request_.options.maximumSearchAngleDegrees +
                  headingEnvelopeSlackDegrees &&
          std::abs(wr::angularDifferenceDegrees(heading, destinationBearing)) >
              duplicateHeadingToleranceDegrees)
        directions.push_back(heading);
    }
    return directions;
  }

  [[nodiscard]] wr::Duration stepFor(const motion::MotionState& state,
                                     bool coastal) const {
    auto step = request_.options.timeStep;
    const auto minimum = std::min(
        step,
        std::max(
            maximumIntegrationSlice,
            std::max(
                {request_.options.minimumTimeStep,
                 request_.vessel.tackPenalty + minimumMovingTimeAfterManoeuvre,
                 request_.vessel.gybePenalty + minimumMovingTimeAfterManoeuvre,
                 request_.vessel.sailPlanChangePenalty +
                     minimumMovingTimeAfterManoeuvre})));
    if (coastal) step = std::min(step, std::max(minimum, options_.coastalStep));
    if (remaining(state) <
        options_.guidanceSpeedKnots * static_cast<double>(step.count()) / 1800.)
      step = std::max(minimum, step / 4);
    return std::min(
        step, std::chrono::duration_cast<wr::Duration>(
                  request_.departure + request_.limits.maximumRouteDuration -
                  state.time));
  }

  void checkObservedSpeed(const motion::MotionState& from,
                          const motion::MotionCandidate& candidate,
                          double endpointToleranceNm = 0.0) const {
    const double speed = options_.certifiedMaximumGroundSpeedKnots;
    const double seconds =
        static_cast<double>((candidate.state.time - from.time).count());
    if (speed > 0 && seconds > 0 &&
        (wr::distanceNm(from.position, candidate.state.position) -
         endpointToleranceNm) *
                secondsPerHour >
            speed * (seconds + numericalArrivalSlackSeconds) +
                speedBoundComparisonSlackKnotSeconds)
      throw StopSearch{
          wr::RoutingStatus::InvalidVesselConfiguration,
          "Observed motion violates caller-supplied ground-speed bound"};
  }

  std::span<const double> coastalFan(detail::SearchNode& node,
                                     wr::Duration step) {
    if (node.coastalHeadings.empty()) {
      std::vector<std::pair<double, double>> ranked;
      const double range = options_.guidanceSpeedKnots *
                           static_cast<double>(step.count()) / secondsPerHour;
      for (double heading :
           headings(node.state, options_.coastalHeadingStepDegrees))
        ranked.emplace_back(geography_.movementRank(wr::destinationPoint(
                                node.state.position, heading, range)),
                            heading);
      std::stable_sort(ranked.begin(), ranked.end());
      for (const auto& [estimate, heading] : ranked) {
        (void)estimate;
        node.coastalHeadings.push_back(heading);
      }
    }
    return node.coastalHeadings;
  }

  template <typename Admit>
  unsigned expand(const motion::MotionState& state, wr::Duration step, bool mim,
                  const Admit& admit, unsigned offset = 0,
                  std::span<const double> cached = {}) {
    if (step <= wr::Duration::zero()) return 0;
    const auto fresh = cached.empty()
                           ? headings(state, options_.headingStepDegrees)
                           : std::vector<double>{};
    const std::span<const double> directions =
        cached.empty() ? std::span<const double>{fresh} : cached;
    const auto end = mim ? static_cast<unsigned>(directions.size())
                         : std::min(static_cast<unsigned>(directions.size()),
                                    offset + options_.coastalFanBatch);
    for (unsigned index = offset; index < end; ++index) {
      const double heading = directions[index];
      checkBudget();
      ++output_.search.motionAttempts;
      if (mim)
        ++output_.search.mimMotionAttempts;
      else
        ++output_.search.astarMotionAttempts;
      auto candidates = motion::checkedMotion(
          request_, environment_, performance_, state, heading, step,
          std::min(step, maximumIntegrationSlice), output_.route.diagnostics,
          &dataFailure_);
      for (auto& candidate : candidates) {
        checkObservedSpeed(state, candidate);
        if (output_.route.diagnostics.generatedStates >=
            request_.limits.maximumGeneratedStates)
          throw StopSearch{wr::RoutingStatus::ResourceLimitReached,
                           "Generated-state allowance reached"};
        ++output_.route.diagnostics.generatedStates;
        output_.route.diagnostics.closestApproachNm =
            std::min(output_.route.diagnostics.closestApproachNm,
                     remaining(candidate.state));
        if (admissibleState(candidate.state))
          admit(std::move(candidate));
        else
          ++output_.search.boundPruned;
      }
    }
    if (!offset && request_.options.allowWaiting &&
        state.waitDuration < request_.options.maximumWait) {
      checkBudget();
      ++output_.search.motionAttempts;
      if (mim)
        ++output_.search.mimMotionAttempts;
      else
        ++output_.search.astarMotionAttempts;
      const auto duration =
          std::min(step, request_.options.maximumWait - state.waitDuration);
      auto wait = motion::checkedWait(request_, environment_, state, duration,
                                      output_.route.diagnostics);
      if (wait) {
        ++output_.search.waitCandidates;
        ++output_.route.diagnostics.generatedStates;
        if (admissibleState(wait->state)) admit(std::move(*wait));
      }
    }
    return end < directions.size() ? end : 0;
  }

  void tryGoal(const detail::SearchStorage& storage, std::size_t parent,
               wr::Duration step) {
    const auto& state = storage.nodes.at(parent).state;
    const double radius = std::max(
        1.0, options_.guidanceSpeedKnots *
                 static_cast<double>(request_.options.timeStep.count()) /
                 (secondsPerHour / 2));
    if (remaining(state) > radius ||
        elapsed(state) + lowerBound(state) >= incumbentSeconds())
      return;
    if (output_.search.connectionAttempts >= options_.maximumConnectionAttempts)
      return;
    ++output_.search.connectionAttempts;
    const auto remainingTime =
        request_.departure + request_.limits.maximumRouteDuration - state.time;
    auto goal = motion::checkedConnection(
        request_, environment_, performance_, state,
        std::min(remainingTime, std::max(step * 3, minimumConnectionWindow)),
        output_.route.diagnostics);
    checkCancellation();
    if (!goal ||
        goal->state.time >
            request_.departure + request_.limits.maximumRouteDuration ||
        elapsed(goal->state) >= incumbentSeconds())
      return;
    checkObservedSpeed(state, *goal, numericalEndpointAllowanceNm);
    std::vector<wr::RouteLeg> legs{std::move(goal->leg)};
    for (auto index = parent;
         storage.nodes.at(index).parent != detail::noParent;
         index = storage.nodes.at(index).parent)
      legs.push_back(storage.nodes.at(index).incoming);
    std::reverse(legs.begin(), legs.end());
    if (environment_.landAndBoundaries)
      environment_.landAndBoundaries->prepareValidationRoute(
          legs, request_.constraints.landSafetyMarginNm);
    auto validation = wr::RouteValidator{}.validate(request_, environment_,
                                                    performance_, legs);
    output_.route.diagnostics.validationSamples += validation.samples;
    checkCancellation();
    if (!validation.passed && environment_.landAndBoundaries &&
        environment_.landAndBoundaries
            ->validationFailureRequiresSearchEscalation())
      throw StopSearch{wr::RoutingStatus::ValidationFailure,
                       "Authoritative provider requested search escalation"};
    if (!validation.passed) {
      ++output_.search.rejectedCandidates;
      return;
    }
    // Finish all potentially allocating summary work before replacing the
    // incumbent. An allocation failure must not leave partially updated
    // metrics.
    wr::RoutingResult accepted;
    accepted.legs = std::move(legs);
    accepted.validation = std::move(validation);
    accepted.warnings = output_.route.warnings;
    accepted.status = wr::RoutingStatus::Complete;
    accepted.message =
        "Complete — independently validated coastal MIM/A* experiment";
    motion::summariseRoute(accepted);
    checkCancellation();
    ++output_.search.validatedCandidates;
    if (output_.route.validation.passed)
      ++output_.search.improvements;
    else
      output_.search.firstIncumbentSeconds =
          std::chrono::duration<double>(Clock::now() - started_).count();
    output_.route.legs = std::move(accepted.legs);
    output_.route.validation = std::move(accepted.validation);
    output_.route.metrics = accepted.metrics;
    output_.route.environment = accepted.environment;
    output_.route.sourceTransitions = std::move(accepted.sourceTransitions);
    output_.route.warnings = std::move(accepted.warnings);
    output_.route.status = accepted.status;
    output_.route.message = std::move(accepted.message);
    if (options_.onIncumbent) {
      try {
        options_.onIncumbent(output_.route);
      } catch (...) {
        ++output_.search.observerErrors;
      }
    }
    checkCancellation();
  }

  struct SectorKey {
    std::int64_t time;
    unsigned sector;
    wr::Tack tack;
    wr::PropulsionMode mode;
    wr::ProfileRole role;
    std::string profile;
    int sailPlan;
    bool departureEgress;
    auto operator<=>(const SectorKey&) const = default;
  };

  // The geographic value is useful for ordering but is NOT a certified bound.
  // Only lowerBound() is allowed to eliminate an arrival-time candidate.
  double priority(const motion::MotionState& state) const {
    return elapsed(state) + options_.guidanceWeight *
                                geography_.remainingNm(state.position) *
                                secondsPerHour / options_.guidanceSpeedKnots;
  }

  void runAdaptive() {
    detail::SearchStorage storage(memory_);
    using Queue = std::priority_queue<detail::QueueEntry,
                                      std::pmr::vector<detail::QueueEntry>,
                                      std::greater<detail::QueueEntry>>;
    Queue coast(std::greater<detail::QueueEntry>{},
                std::pmr::vector<detail::QueueEntry>{&memory_});
    Queue deferred(std::greater<detail::QueueEntry>{},
                   std::pmr::vector<detail::QueueEntry>{&memory_});
    std::pmr::map<std::int64_t, std::pmr::vector<std::size_t>> ocean{&memory_};
    std::pmr::map<detail::StateKey, unsigned> primaryCounts{&memory_};

    const auto entry = [&](std::size_t id) {
      const auto& state = storage.nodes.at(id).state;
      return detail::QueueEntry{priority(state), remaining(state), id};
    };
    const auto postpone = [&](std::size_t id) {
      deferred.push(entry(id));
      ++output_.search.deferredLabels;
    };
    const auto coastal = [&](std::size_t id, bool force) {
      auto& count = primaryCounts[partition_.key(storage.nodes.at(id).state)];
      if (!force && count >= options_.labelsPerCell) {
        postpone(id);
        ++output_.search.cellDeferred;  // Deferred competition, not deletion.
      } else {
        ++count;
        coast.push(entry(id));
        ++output_.search.coastalTransitions;
      }
    };
    const auto dispatch = [&](std::size_t id) {
      const auto& state = storage.nodes.at(id).state;
      if (geography_.coastal(state.position))
        coastal(id, false);
      else
        ocean[state.time.time_since_epoch().count()].push_back(id);
    };
    dispatch(append(storage, {initialState(), {}}, detail::noParent));
    std::uint64_t jobs = 0;
    while (!coast.empty() || !ocean.empty() || !deferred.empty()) {
      checkBudget();
      // A fixed fraction of scheduling turns revisits deferred choices. This
      // prevents an endlessly progressing wrong approach starving its siblings.
      std::optional<detail::QueueEntry> resumed;
      if (!deferred.empty() &&
          ((coast.empty() && ocean.empty()) ||
           (!coast.empty() && options_.deferredServicePeriod && jobs &&
            jobs % options_.deferredServicePeriod == 0))) {
        resumed = deferred.top();
        deferred.pop();
        ++output_.search.deferredResumed;
      }
      ++jobs;
      double oceanPriority = std::numeric_limits<double>::infinity();
      auto oceanChoice = ocean.end();
      for (auto bucket = ocean.begin(); bucket != ocean.end(); ++bucket) {
        for (const auto id : bucket->second) {
          const double candidate = priority(storage.nodes.at(id).state);
          if (candidate < oceanPriority) {
            oceanPriority = candidate;
            oceanChoice = bucket;
          }
        }
      }
      const bool useCoast =
          resumed || (!coast.empty() &&
                      (ocean.empty() ||
                       coast.top().estimatedTotalSeconds <= oceanPriority));
      if (useCoast) {
        const auto work = resumed ? *resumed : coast.top();
        const auto id = work.node;
        if (!resumed) coast.pop();
        // Keep primary representatives recorded after expansion. Clearing the
        // cell here would repeatedly promote nearly identical later arrivals.
        // Excess histories remain deferred and can still be explicitly resumed.
        ++output_.search.expandedLabels;
        const auto state = storage.nodes.at(id).state;
        if (!admissibleState(state)) {
          ++output_.search.boundPruned;
          continue;
        }
        const auto step = stepFor(state, true);
        if (!work.headingOffset) tryGoal(storage, id, step);
        if (output_.route.validation.passed &&
            !options_.continueAfterFirstRoute) {
          output_.search.stopReason = "First independently validated route";
          return;
        }
        unsigned moving = 0;
        const auto nextHeading = expand(
            state, step, false,
            [&](motion::MotionCandidate candidate) {
              if (!candidate.leg.stationaryWait) ++moving;
              dispatch(append(storage, std::move(candidate), id));
            },
            work.headingOffset, coastalFan(storage.nodes.at(id), step));
        if (nextHeading) {
          auto continuation = entry(id);
          continuation.headingOffset = nextHeading;
          if (!moving)
            coast.push(continuation);
          else {
            deferred.push(continuation);
            ++output_.search.deferredLabels;
          }
        }
      } else if (!ocean.empty()) {
        auto batch = std::move(oceanChoice->second);
        ocean.erase(oceanChoice);
        ++output_.search.mimLayers;
        std::pmr::map<SectorKey, std::pmr::vector<std::size_t>> choices{
            &memory_};
        for (const auto parent : batch) {
          const auto state = storage.nodes.at(parent).state;
          if (!admissibleState(state)) {
            ++output_.search.boundPruned;
            continue;
          }
          const auto step = stepFor(state, false);
          tryGoal(storage, parent, step);
          if (output_.route.validation.passed &&
              !options_.continueAfterFirstRoute) {
            output_.search.stopReason = "First independently validated route";
            return;
          }
          unsigned survivors = 0;
          expand(state, step, true, [&](motion::MotionCandidate candidate) {
            ++survivors;
            const auto id = append(storage, std::move(candidate), parent);
            const auto& next = storage.nodes.at(id).state;
            if (geography_.coastal(next.position)) {
              coastal(id, false);
              return;
            }
            const auto sector = std::min(
                options_.sectors - 1,
                static_cast<unsigned>(
                    wr::initialBearingDegrees(request_.start, next.position) *
                    options_.sectors / fullTurnDegrees));
            choices[{next.time.time_since_epoch().count(), sector, next.tack,
                     next.mode, next.role, next.profileIdentity, next.sailPlan,
                     next.departureEgressActive}]
                .push_back(id);
          });
          if (!survivors && step > request_.options.minimumTimeStep) {
            coastal(parent, true);
            ++output_.search.blockedOceanEscalations;
          }
        }
        for (auto& [key, alternatives] : choices) {
          std::stable_sort(
              alternatives.begin(), alternatives.end(), [&](auto a, auto b) {
                return std::tuple{priority(storage.nodes.at(a).state), a} <
                       std::tuple{priority(storage.nodes.at(b).state), b};
              });
          // MIM keeps a compact active front. All other physical histories are
          // retained for later recovery, subject to the explicit storage limit.
          for (std::size_t i = 0; i < alternatives.size(); ++i) {
            const auto id = alternatives[i];
            if (i < 2)
              ocean[key.time].push_back(id);
            else
              postpone(id);
          }
        }
      }
      output_.search.peakQueueEntries = std::max(
          output_.search.peakQueueEntries, coast.size() + deferred.size());
    }
    output_.search.astarQueueExhausted = true;
  }

  // This noncopyable session borrows caller objects only for route()'s
  // lifetime. References express that none may be null or rebound; C.12's
  // regular-value concern does not apply to this scoped search controller.
  // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
  const wr::RoutingRequest& request_;
  const wr::RoutingEnvironment& environment_;
  const Options& options_;
  const wr::VesselPerformanceModel& performance_;
  Result& output_;
  // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
  detail::StatePartition partition_;
  detail::BudgetMemoryResource memory_;
  Clock::time_point started_;
  detail::GeographyHints geography_;
  wr::RoutingStatus dataFailure_{wr::RoutingStatus::Complete};
};
}  // namespace

Result Router::route(const wr::RoutingRequest& request,
                     const wr::RoutingEnvironment& environment,
                     const Options& options) const {
  Result result;
  result.route.engineIdentity = "mim_coastal_prototype_v1";
  result.route.searchVariant =
      options.coastEverywhere ? "coastal_astar" : "adaptive_mim_coastal_astar";
  result.search.usesCertifiedBound =
      options.certifiedMaximumGroundSpeedKnots > 0;
  result.search.approximateSearch =
      true;  // Finite headings, numerical integration and work limits.
  if (!validOptions(options) ||
      request.options.timeStep <= wr::Duration::zero() ||
      request.limits.maximumRouteDuration <= wr::Duration::zero() ||
      request.limits.maximumRetainedStates == 0 ||
      request.limits.maximumGeneratedStates == 0) {
    result.route.status = wr::RoutingStatus::InvalidVesselConfiguration;
    result.route.message = "Invalid MIM/A* options or request limits";
    return result;
  }
  if (!validPoint(request.start) || !validPoint(request.destination)) {
    result.route.status = !validPoint(request.start)
                              ? wr::RoutingStatus::InvalidStart
                              : wr::RoutingStatus::InvalidDestination;
    result.route.message =
        "MIM/A* prototype requires finite endpoints within 85 degrees latitude";
    return result;
  }
  if (request.objective.kind != wr::ObjectiveKind::Fastest &&
      request.objective.kind != wr::ObjectiveKind::FastestUnderSafetyLimits) {
    result.route.status = wr::RoutingStatus::InvalidVesselConfiguration;
    result.route.message =
        "MIM/A* prototype supports deterministic minimum-time objectives";
    return result;
  }
  try {
    if (request.cancellation.cancelled()) {
      result.route.status = wr::RoutingStatus::Cancelled;
      result.route.message = "MIM/A* request cancelled";
      return result;
    }
    result.route.preflight =
        wr::RoutingEngine{}.preflight(request, environment);
    if (request.cancellation.cancelled()) {
      result.route.status = wr::RoutingStatus::Cancelled;
      result.route.message = "MIM/A* request cancelled during preflight";
      return result;
    }
    result.route.warnings = result.route.preflight->warnings;
    if (!result.route.preflight->canRoute) {
      result.route.status =
          motion::failedPreflightStatus(*result.route.preflight);
      result.route.message = "MIM/A* preflight requires caller action";
      return result;
    }
    if (environment.xtdCurrent && request.environment.useCurrent &&
        result.route.preflight->coverage.currentFallbackNeeded) {
      result.route.status = wr::RoutingStatus::WeatherCoverageInsufficient;
      result.route.message =
          "Prototype requires current prediction coverage to be prepared by "
          "caller";
      return result;
    }
    if (environment.landAndBoundaries &&
        (environment.landAndBoundaries->pointForbidden(request.start) ||
         environment.landAndBoundaries->pointForbidden(request.destination))) {
      result.route.status =
          environment.landAndBoundaries->pointForbidden(request.start)
              ? wr::RoutingStatus::InvalidStart
              : wr::RoutingStatus::InvalidDestination;
      result.route.message = "Endpoint forbidden by supplied geography";
      return result;
    }
    wr::PolarPerformanceModel fallback(request.vessel);
    const auto& performance =
        environment.performance ? *environment.performance : fallback;
    SearchSession{request, environment, options, performance, result}.run();
  } catch (const std::bad_alloc&) {
    result.route.status = wr::RoutingStatus::ResourceLimitReached;
    result.route.message = "Allocation failed outside managed search storage";
    result.route.legs.clear();
    result.route.validation = {};
    result.route.metrics = {};
  } catch (const std::exception& exception) {
    result.route.status = wr::RoutingStatus::InternalError;
    result.route.message = exception.what();
    result.route.legs.clear();
    result.route.validation = {};
    result.route.metrics = {};
  }
  return result;
}
}  // namespace mim_coastal
