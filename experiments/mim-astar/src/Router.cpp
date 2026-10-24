// SPDX-License-Identifier: GPL-3.0-or-later
#include "mim_astar/Router.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "MotionKernel.h"
#include "SearchStorage.h"
#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/Providers.h"
#include "supercpn/weather_routing/Types.h"

namespace mim_astar {
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
  [[nodiscard]] const char* what() const noexcept override { return reason.c_str(); }
  wr::RoutingStatus status;
  std::string reason;
};

bool validPoint(wr::GeoPoint point) {
  return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
         std::abs(point.latitude) <= maximumLatitudeDegrees &&
         std::abs(point.longitude) <= halfTurnDegrees;
}

bool validOptions(const Options& options) {
  const bool knownStrategy = options.strategy == Strategy::MimOnly ||
                             options.strategy == Strategy::AStarOnly ||
                             options.strategy == Strategy::MimThenAStar;
  return knownStrategy && options.sectors >= minimumSectorCount &&
         options.sectors <= fullTurnDegrees && options.labelsPerCell > 0 &&
         options.labelsPerCell <= maximumLabelsPerCell &&
         std::isfinite(options.cellSizeNm) && options.cellSizeNm >= minimumCellSizeNm &&
         options.cellSizeNm <= maximumCellSizeNm &&
         std::isfinite(options.headingStepDegrees) && options.headingStepDegrees >= 1 &&
         options.headingStepDegrees <= quarterTurnDegrees &&
         std::isfinite(options.headingBucketDegrees) &&
         options.headingBucketDegrees >= 1 &&
         options.headingBucketDegrees <= quarterTurnDegrees &&
         std::isfinite(options.guidanceSpeedKnots) && options.guidanceSpeedKnots > 0 &&
         std::isfinite(options.certifiedMaximumGroundSpeedKnots) &&
         options.certifiedMaximumGroundSpeedKnots >= 0 &&
         options.maximumMotionAttempts > 0 && options.maximumConnectionAttempts > 0 &&
         options.maximumLabels > 0 && options.maximumWeatherSamples > 0 &&
         options.searchMemoryBytes >= minimumSearchMemoryBytes &&
         std::isfinite(options.mimWorkFraction) && options.mimWorkFraction > 0 &&
         options.mimWorkFraction < 1;
}

// One request owns physics references, budgets and the validated incumbent.
// SearchStorage is separate so MIM history can be freed before A* restarts.
class SearchSession {
public:
  SearchSession(const wr::RoutingRequest& request,
                const wr::RoutingEnvironment& environment, const Options& options,
                const wr::VesselPerformanceModel& performance, Result& result)
      : request_(request),
        environment_(environment),
        options_(options),
        performance_(performance),
        output_(result),
        partition_(request, options),
        memory_(options.searchMemoryBytes),
        started_(Clock::now()) {}

  void run() {
    try {
      if (options_.strategy != Strategy::AStarOnly) {
        mimStage_ = true;
        try {
          runMim();
        } catch (const std::bad_alloc&) {
          if (options_.strategy != Strategy::MimThenAStar) throw;
          // Unwinding frees the MIM containers before A* gets a fresh arena.
          output_.search.mimYieldReason = "MIM storage exhausted; retrying with A*";
        }
        mimStage_ = false;
      }
      if (options_.strategy != Strategy::MimOnly) runAStar();
      checkCancellation();
      if (!output_.route.validation.passed) {
        output_.route.status = dataFailure_ == wr::RoutingStatus::Complete
                                   ? wr::RoutingStatus::SearchIncomplete
                                   : dataFailure_;
        output_.route.message = "Bounded MIM/A* search found no validated route";
      }
      if (output_.search.stopReason.empty())
        output_.search.stopReason = "Configured search stages finished";
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
    output_.search.resourceLimited = status == wr::RoutingStatus::ResourceLimitReached;
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
      throw StopSearch{wr::RoutingStatus::Cancelled, "MIM/A* request cancelled"};
  }

  void checkBudget() const {
    checkCancellation();
    if (output_.search.motionAttempts >= options_.maximumMotionAttempts ||
        output_.route.diagnostics.generatedStates >=
            request_.limits.maximumGeneratedStates ||
        output_.route.diagnostics.weatherSamples >= options_.maximumWeatherSamples)
      throw StopSearch{wr::RoutingStatus::ResourceLimitReached,
                       "Motion, generated-state or weather allowance reached"};
  }

  [[nodiscard]] std::uint64_t mimShare(std::uint64_t total) const {
    return static_cast<std::uint64_t>(static_cast<long double>(total) *
                                      options_.mimWorkFraction);
  }

  bool yieldMim(const char* reason) {
    mimYieldRequested_ = true;
    output_.search.mimYieldReason = reason;
    return false;
  }

  // A hybrid reserves work for a fresh A* search. Returning false abandons the
  // current MIM layer, just as reaching the stage's motion allowance does.
  [[nodiscard]] bool mimShareAvailable() {
    checkCancellation();
    if (!mimStage_ || options_.strategy != Strategy::MimThenAStar) return true;
    if (output_.search.motionAttempts >= mimShare(options_.maximumMotionAttempts))
      return yieldMim("MIM motion share reached; remaining work reserved for A*");
    if (output_.route.diagnostics.weatherSamples >=
        mimShare(options_.maximumWeatherSamples))
      return yieldMim("MIM weather share reached; remaining work reserved for A*");
    if (output_.route.diagnostics.generatedStates >=
        mimShare(request_.limits.maximumGeneratedStates))
      return yieldMim(
          "MIM generated-state share reached; remaining work reserved for A*");
    if (output_.search.connectionAttempts >= mimShare(options_.maximumConnectionAttempts))
      return yieldMim("MIM connection share reached; remaining work reserved for A*");
    return true;
  }

  [[nodiscard]] bool mimStorageAvailable(const detail::SearchStorage& storage) {
    const auto limit =
        std::min(options_.maximumLabels, request_.limits.maximumRetainedStates);
    if (options_.strategy == Strategy::MimThenAStar &&
        storage.nodes.size() >= mimShare(limit))
      return yieldMim("MIM lineage share reached; remaining storage reserved for A*");
    return true;
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
    const double distance = std::max(
        0.0, remaining(state) - std::max(numericalEndpointAllowanceNm,
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
    return state.time <= request_.departure + request_.limits.maximumRouteDuration &&
           wr::distanceNm(request_.start, state.position) <=
               request_.limits.maximumExplorationDistanceNm &&
           elapsed(state) + lowerBound(state) < incumbentSeconds();
  }

  [[nodiscard]] motion::MotionState initialState() const {
    motion::MotionState initial;
    initial.position = request_.start;
    initial.time = request_.departure;
    if (environment_.landAndBoundaries && request_.constraints.landSafetyMarginNm > 0)
      initial.departureEgressActive =
          environment_.landAndBoundaries->distanceToForbiddenNm(request_.start) <
          request_.constraints.landSafetyMarginNm;
    return initial;
  }

  std::size_t append(detail::SearchStorage& storage, motion::MotionCandidate candidate,
                     std::size_t parent) {
    const auto limit =
        std::min(options_.maximumLabels, request_.limits.maximumRetainedStates);
    if (storage.nodes.size() >= limit)
      throw StopSearch{wr::RoutingStatus::ResourceLimitReached,
                       "Retained lineage-label allowance reached"};
    storage.nodes.push_back(
        {std::move(candidate.state), std::move(candidate.leg), parent});
    output_.search.retainedLabels =
        std::max<std::uint64_t>(output_.search.retainedLabels, storage.nodes.size());
    output_.route.diagnostics.retainedStates = output_.search.retainedLabels;
    return storage.nodes.size() - 1;
  }

  [[nodiscard]] std::vector<double> headings(const motion::MotionState& state) const {
    const double destinationBearing =
        wr::initialBearingDegrees(state.position, request_.destination);
    std::vector<double> directions{destinationBearing};
    const unsigned count =
        static_cast<unsigned>(std::ceil(fullTurnDegrees / options_.headingStepDegrees));
    for (unsigned index = 0; index < count; ++index) {
      const double heading = fullTurnDegrees * index / count;
      if (std::abs(wr::angularDifferenceDegrees(heading, destinationBearing)) <=
              request_.options.maximumSearchAngleDegrees + headingEnvelopeSlackDegrees &&
          std::abs(wr::angularDifferenceDegrees(heading, destinationBearing)) >
              duplicateHeadingToleranceDegrees)
        directions.push_back(heading);
    }
    return directions;
  }

  [[nodiscard]] wr::Duration stepFor(const motion::MotionState& state) const {
    auto step = request_.options.timeStep;
    const auto minimum = std::min(
        step,
        std::max(maximumIntegrationSlice,
                 std::max({request_.options.minimumTimeStep,
                           request_.vessel.tackPenalty + minimumMovingTimeAfterManoeuvre,
                           request_.vessel.gybePenalty + minimumMovingTimeAfterManoeuvre,
                           request_.vessel.sailPlanChangePenalty +
                               minimumMovingTimeAfterManoeuvre})));
    if (options_.adaptiveSteps) {
      const double travelNm = options_.guidanceSpeedKnots *
                              static_cast<double>(step.count()) / secondsPerHour;
      if (remaining(state) < 2 * travelNm) step = std::max(minimum, step / 4);
      if (environment_.landAndBoundaries) {
        const double clearance =
            environment_.landAndBoundaries->distanceToForbiddenNm(state.position);
        if (std::isfinite(clearance) && clearance < travelNm)
          step = std::max(minimum, step / 4);
      }
    }
    return std::min(step, std::chrono::duration_cast<wr::Duration>(
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
        (wr::distanceNm(from.position, candidate.state.position) - endpointToleranceNm) *
                secondsPerHour >
            speed * (seconds + numericalArrivalSlackSeconds) +
                speedBoundComparisonSlackKnotSeconds)
      throw StopSearch{wr::RoutingStatus::InvalidVesselConfiguration,
                       "Observed motion violates caller-supplied ground-speed bound"};
  }

  template <typename Admit>
  void expand(const motion::MotionState& state, wr::Duration step, bool mim,
              const Admit& admit) {
    if (step <= wr::Duration::zero()) return;
    for (double heading : headings(state)) {
      if (!mimShareAvailable()) return;
      checkBudget();
      ++output_.search.motionAttempts;
      if (mim)
        ++output_.search.mimMotionAttempts;
      else
        ++output_.search.astarMotionAttempts;
      auto candidates =
          motion::checkedMotion(request_, environment_, performance_, state, heading,
                                step, std::min(step, maximumIntegrationSlice),
                                output_.route.diagnostics, &dataFailure_);
      for (auto& candidate : candidates) {
        checkObservedSpeed(state, candidate);
        if (output_.route.diagnostics.generatedStates >=
            request_.limits.maximumGeneratedStates)
          throw StopSearch{wr::RoutingStatus::ResourceLimitReached,
                           "Generated-state allowance reached"};
        ++output_.route.diagnostics.generatedStates;
        output_.route.diagnostics.closestApproachNm = std::min(
            output_.route.diagnostics.closestApproachNm, remaining(candidate.state));
        if (admissibleState(candidate.state))
          admit(std::move(candidate));
        else
          ++output_.search.boundPruned;
      }
    }
    if (request_.options.allowWaiting &&
        state.waitDuration < request_.options.maximumWait) {
      if (!mimShareAvailable()) return;
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
  }

  void tryGoal(const detail::SearchStorage& storage, std::size_t parent,
               wr::Duration step) {
    if (!mimShareAvailable()) return;
    const auto& state = storage.nodes.at(parent).state;
    const double radius =
        std::max(1.0, options_.guidanceSpeedKnots *
                          static_cast<double>(request_.options.timeStep.count()) /
                          (secondsPerHour / 2));
    if (remaining(state) > radius ||
        elapsed(state) + lowerBound(state) >= incumbentSeconds())
      return;
    if (output_.search.connectionAttempts >= options_.maximumConnectionAttempts) return;
    ++output_.search.connectionAttempts;
    auto goal = motion::checkedConnection(request_, environment_, performance_, state,
                                          std::max(step * 3, minimumConnectionWindow),
                                          output_.route.diagnostics);
    checkCancellation();
    if (!goal ||
        goal->state.time > request_.departure + request_.limits.maximumRouteDuration ||
        elapsed(goal->state) >= incumbentSeconds())
      return;
    checkObservedSpeed(state, *goal, numericalEndpointAllowanceNm);
    std::vector<wr::RouteLeg> legs{std::move(goal->leg)};
    for (auto index = parent; storage.nodes.at(index).parent != detail::noParent;
         index = storage.nodes.at(index).parent)
      legs.push_back(storage.nodes.at(index).incoming);
    std::reverse(legs.begin(), legs.end());
    if (environment_.landAndBoundaries)
      environment_.landAndBoundaries->prepareValidationRoute(
          legs, request_.constraints.landSafetyMarginNm);
    auto validation =
        wr::RouteValidator{}.validate(request_, environment_, performance_, legs);
    output_.route.diagnostics.validationSamples += validation.samples;
    checkCancellation();
    if (!validation.passed && environment_.landAndBoundaries &&
        environment_.landAndBoundaries->validationFailureRequiresSearchEscalation())
      throw StopSearch{wr::RoutingStatus::ValidationFailure,
                       "Authoritative provider requested search escalation"};
    if (!validation.passed) {
      ++output_.search.rejectedCandidates;
      return;
    }
    // Finish all potentially allocating summary work before replacing the
    // incumbent. An allocation failure must not leave partially updated metrics.
    wr::RoutingResult accepted;
    accepted.legs = std::move(legs);
    accepted.validation = std::move(validation);
    accepted.warnings = output_.route.warnings;
    accepted.status = wr::RoutingStatus::Complete;
    accepted.message = "Complete — independently validated MIM/A* prototype";
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
    unsigned sector;
    wr::Tack tack;
    wr::PropulsionMode mode;
    wr::ProfileRole role;
    std::string profileIdentity;
    int sailPlan;
    bool departureEgressActive;
    auto operator<=>(const SectorKey&) const = default;
  };

  struct Proposal {
    motion::MotionCandidate motion;
    std::size_t parent{detail::noParent};
    double progress{};
    double priority{};
  };

  void retainMimProposal(std::pmr::vector<Proposal>& choices, Proposal proposal) {
    choices.push_back(std::move(proposal));
    std::stable_sort(choices.begin(), choices.end(),
                     [](const auto& first, const auto& second) {
                       return first.priority < second.priority;
                     });
    if (choices.size() <= 2) return;
    // Keep the lowest estimated arrival plus an outward alternative. Retaining
    // only the nearest-to-goal candidate tends to discard useful detours.
    auto farthest = std::max_element(choices.begin() + 1, choices.end(),
                                     [](const auto& first, const auto& second) {
                                       return first.progress < second.progress;
                                     });
    if (farthest != choices.begin() + 1) std::swap(*farthest, choices.at(1));
    choices.resize(2);
    ++output_.search.cellPruned;
  }

  void runMim() {
    detail::SearchStorage storage(memory_);
    if (!mimStorageAvailable(storage)) return;
    append(storage, {initialState(), {}}, detail::noParent);
    std::pmr::vector<std::size_t> frontier{&memory_};
    frontier.push_back(0);
    const auto allowance =
        options_.strategy == Strategy::MimOnly
            ? options_.maximumMotionAttempts
            : static_cast<std::uint64_t>(
                  static_cast<long double>(options_.maximumMotionAttempts) *
                  options_.mimWorkFraction);
    while (!frontier.empty() && output_.search.motionAttempts < allowance) {
      if (!mimShareAvailable()) return;
      checkBudget();
      ++output_.search.mimLayers;
      auto step = request_.options.timeStep;
      if (options_.adaptiveSteps) {
        double nearest = std::numeric_limits<double>::infinity();
        for (auto parent : frontier)
          nearest = std::min(nearest, remaining(storage.nodes.at(parent).state));
        const double arrivalRadius = options_.guidanceSpeedKnots *
                                     static_cast<double>(step.count()) /
                                     (secondsPerHour * 2);
        if (nearest < arrivalRadius)
          step = std::min(step, std::max(minimumMimArrivalStep, step / 4));
      }
      for (auto parent : frontier)
        step =
            std::min(step, std::chrono::duration_cast<wr::Duration>(
                               request_.departure + request_.limits.maximumRouteDuration -
                               storage.nodes.at(parent).state.time));
      std::pmr::map<SectorKey, std::pmr::vector<Proposal>> sectors{&memory_};
      for (auto parent : frontier) {
        tryGoal(storage, parent, step);
        if (mimYieldRequested_) return;
        if (output_.route.validation.passed) return;
        if (output_.search.motionAttempts >= allowance) break;
        expand(storage.nodes.at(parent).state, step, true,
               [&](motion::MotionCandidate candidate) {
                 const auto& state = candidate.state;
                 const unsigned sector =
                     std::min(options_.sectors - 1,
                              static_cast<unsigned>(wr::initialBearingDegrees(
                                                        request_.start, state.position) *
                                                    options_.sectors / fullTurnDegrees));
                 SectorKey key{sector,
                               state.tack,
                               state.mode,
                               state.role,
                               state.profileIdentity,
                               state.sailPlan,
                               state.departureEgressActive};
                 const double progress = wr::distanceNm(request_.start, state.position);
                 const double priority = elapsed(state) + remaining(state) *
                                                              secondsPerHour /
                                                              options_.guidanceSpeedKnots;
                 retainMimProposal(sectors[key],
                                   {std::move(candidate), parent, progress, priority});
               });
        if (mimYieldRequested_) return;
      }
      frontier.clear();
      for (auto& [sector, choices] : sectors)
        for (auto& choice : choices) {
          if (!mimStorageAvailable(storage)) return;
          frontier.push_back(append(storage, std::move(choice.motion), choice.parent));
        }
    }
  }

  void runAStar() {
    // A* starts at departure instead of inheriting MIM's discarded lineages.
    // The validated MIM route survives separately as a cost bound/incumbent.
    detail::SearchStorage storage(memory_);
    const auto root = append(storage, {initialState(), {}}, detail::noParent);
    storage.queue.push({lowerBound(storage.nodes.at(root).state),
                        remaining(storage.nodes.at(root).state), root});
    const auto expansionStart = output_.search.expandedLabels;
    std::optional<std::uint64_t> firstIncumbentExpansion;
    if (output_.route.validation.passed) firstIncumbentExpansion = expansionStart;
    while (!storage.queue.empty()) {
      checkBudget();
      if (firstIncumbentExpansion &&
          output_.search.expandedLabels - *firstIncumbentExpansion >=
              options_.maximumImprovementExpansions) {
        output_.search.stopReason = "Bounded A* improvement allowance reached";
        return;
      }
      const auto entry = storage.queue.top();
      storage.queue.pop();
      const auto& node = storage.nodes.at(entry.node);
      if (!node.active) {
        ++output_.search.staleQueueEntries;
        continue;
      }
      if (entry.lowerBoundSeconds >= incumbentSeconds()) {
        // This closes only the retained approximate search. Spatial quotas and
        // sampled headings prevent a claim of continuous global optimality.
        output_.search.stopReason =
            "Remaining retained A* bounds cannot improve incumbent";
        return;
      }
      ++output_.search.expandedLabels;
      const auto step = stepFor(node.state);
      tryGoal(storage, entry.node, step);
      if (output_.route.validation.passed && !firstIncumbentExpansion)
        firstIncumbentExpansion = output_.search.expandedLabels;
      // deque references remain valid as descendants are appended.
      expand(node.state, step, false, [&](motion::MotionCandidate candidate) {
        const double priority = elapsed(candidate.state) + lowerBound(candidate.state);
        auto& alternatives = storage.cells[partition_.key(candidate.state)];
        if (!alternatives.empty() && options_.exactStateKeys) {
          ++output_.search.cellPruned;
          return;  // Exactly identical continuation state.
        }
        if (alternatives.size() >= options_.labelsPerCell) {
          ++output_.search.cellPruned;
          return;  // Explicit lossy approximation, never an exact dominance claim.
        }
        const double distance = remaining(candidate.state);
        const auto index = append(storage, std::move(candidate), entry.node);
        alternatives.push_back(index);
        storage.queue.push({priority, distance, index});
        output_.search.peakQueueEntries =
            std::max(output_.search.peakQueueEntries, storage.queue.size());
      });
    }
    output_.search.astarQueueExhausted = true;
  }

  // This noncopyable session borrows caller objects only for route()'s lifetime.
  // References express that none may be null or rebound; C.12's regular-value
  // concern does not apply to this scoped search controller.
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
  bool mimStage_{};
  bool mimYieldRequested_{};
  wr::RoutingStatus dataFailure_{wr::RoutingStatus::Complete};
};
}  // namespace

Result Router::route(const wr::RoutingRequest& request,
                     const wr::RoutingEnvironment& environment,
                     const Options& options) const {
  Result result;
  result.route.engineIdentity = "mim_astar_prototype_v2";
  result.route.searchVariant = options.strategy == Strategy::MimOnly     ? "mim"
                               : options.strategy == Strategy::AStarOnly ? "astar"
                                                                         : "mim_astar";
  result.search.usesCertifiedBound = options.certifiedMaximumGroundSpeedKnots > 0;
  result.search.approximatePruning =
      options.strategy != Strategy::AStarOnly || !options.exactStateKeys;
  if (!validOptions(options) || request.options.timeStep <= wr::Duration::zero() ||
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
    result.route.preflight = wr::RoutingEngine{}.preflight(request, environment);
    if (request.cancellation.cancelled()) {
      result.route.status = wr::RoutingStatus::Cancelled;
      result.route.message = "MIM/A* request cancelled during preflight";
      return result;
    }
    result.route.warnings = result.route.preflight->warnings;
    if (!result.route.preflight->canRoute) {
      result.route.status = motion::failedPreflightStatus(*result.route.preflight);
      result.route.message = "MIM/A* preflight requires caller action";
      return result;
    }
    if (environment.xtdCurrent && request.environment.useCurrent &&
        result.route.preflight->coverage.currentFallbackNeeded) {
      result.route.status = wr::RoutingStatus::WeatherCoverageInsufficient;
      result.route.message =
          "Prototype requires current prediction coverage to be prepared by caller";
      return result;
    }
    if (environment.landAndBoundaries &&
        (environment.landAndBoundaries->pointForbidden(request.start) ||
         environment.landAndBoundaries->pointForbidden(request.destination))) {
      result.route.status = environment.landAndBoundaries->pointForbidden(request.start)
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
}  // namespace mim_astar
