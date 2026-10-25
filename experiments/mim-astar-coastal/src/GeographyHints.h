// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <tuple>
#include <vector>
#include "mim_coastal/Router.h"

namespace mim_coastal::detail {

// Request-local, bounded hints. This class never authorises vessel motion.
// A missed island or an impassable coarse-grid channel can change search order,
// but all delivered moves still go through the original provider and replay.
class GeographyHints {
public:
  GeographyHints(const wr::RoutingRequest& request,
                 const wr::RoutingEnvironment& env, const Options& options,
                 Diagnostics& diagnostics)
      : request_(request),
        options_(options),
        diagnostics_(diagnostics),
        provider_(options.geographyGuidance ? options.geographyGuidance
                                            : env.landAndBoundaries),
        longitudeScale_(
            60 * std::cos(request.start.latitude * std::acos(-1.) / 180.)) {}

  GeographyHints(const GeographyHints&) = delete;
  GeographyHints& operator=(const GeographyHints&) = delete;
  GeographyHints(GeographyHints&&) = delete;
  GeographyHints& operator=(GeographyHints&&) = delete;

  bool coastal(wr::GeoPoint point) {
    if (options_.coastEverywhere) {
      prepareField();
      return true;
    }
    if (!provider_ || !options_.guidanceEnabled) return false;
    const auto [x, y] = coordinates(point);
    const double cell = std::max(.25, options_.coastalBandNm / 2);
    const auto key = std::pair{static_cast<int>(std::floor(x / cell)),
                               static_cast<int>(std::floor(y / cell))};
    auto found = clearance_.find(key);
    if (found == clearance_.end()) {
      if (diagnostics_.hintQueries >= options_.maximumGuidanceQueries) {
        ++diagnostics_.hintUnavailable;
        return false;  // Unknown hint: use checked MIM, never declare land.
      }
      const auto centre =
          position((key.first + .5) * cell, (key.second + .5) * cell);
      ++diagnostics_.hintQueries;
      const double distance = provider_->distanceToForbiddenNm(centre);
      if (clearance_.size() >= options_.maximumHintCacheCells) {
        clearance_.erase(insertionOrder_.front());
        insertionOrder_.pop_front();
      }
      found = clearance_.emplace(key, distance).first;
      insertionOrder_.push_back(key);
      diagnostics_.hintCacheCells = clearance_.size();
    } else
      ++diagnostics_.hintCacheHits;
    const auto centre =
        position((key.first + .5) * cell, (key.second + .5) * cell);
    const double lower = found->second - wr::distanceNm(point, centre);
    const double lookahead = std::min(
        12., options_.guidanceSpeedKnots *
                 static_cast<double>(request_.options.timeStep.count()) /
                 2400.);
    const bool nearby =
        !std::isfinite(lower)
            ? !(std::isinf(lower) && lower > 0)
            : lower <= std::max(options_.coastalBandNm, lookahead) +
                           request_.constraints.landSafetyMarginNm;
    if (nearby) prepareField();
    return nearby;
  }

  double remainingNm(wr::GeoPoint point) const {
    const double direct = wr::distanceNm(point, request_.destination);
    const auto index = cellAt(point);
    if (!index || !std::isfinite(cost_[*index])) return direct;
    // The grid is an estimate, not a lower bound. Never prune or certify a path
    // using it, including when its finite domain omits a distant detour.
    double estimate = std::numeric_limits<double>::infinity();
    const auto [localX, localY] = coordinates(point);
    const int x = static_cast<int>(*index % width_),
              y = static_cast<int>(*index / width_);
    // Min-plus interpolation avoids artificial cost hills at coarse cell edges.
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int xx = x + dx, yy = y + dy;
        if (xx < 0 || yy < 0 || xx >= static_cast<int>(width_) ||
            yy >= static_cast<int>(height_))
          continue;
        const auto next =
            static_cast<unsigned>(yy) * width_ + static_cast<unsigned>(xx);
        estimate = std::min(
            estimate,
            cost_[next] +
                std::hypot(
                    localX - (west_ + (next % width_ + .5) * spacing_),
                    localY - (south_ + (next / width_ + .5) * spacing_)));
      }
    return std::max(direct, std::min(estimate, direct * 3 + 20));
  }

  double movementRank(wr::GeoPoint point) const {
    const auto index = cellAt(point);
    const double penalty =
        index && !water_[*index] ? 4 * (options_.coastalBandNm + spacing_) : 0.;
    return remainingNm(point) +
           penalty;  // Ordering only; no move is discarded.
  }

  std::optional<double> direction(wr::GeoPoint point) const {
    const auto index = cellAt(point);
    if (!index || !std::isfinite(cost_[*index])) return {};
    unsigned best = *index;
    double value = cost_[best];
    const int x = static_cast<int>(best % width_),
              y = static_cast<int>(best / width_);
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int xx = x + dx, yy = y + dy;
        if (xx < 0 || yy < 0 || xx >= static_cast<int>(width_) ||
            yy >= static_cast<int>(height_))
          continue;
        const auto next =
            static_cast<unsigned>(yy) * width_ + static_cast<unsigned>(xx);
        if (cost_[next] < value) {
          best = next;
          value = cost_[next];
        }
      }
    if (best == *index) return {};
    return wr::initialBearingDegrees(point, centre(best));
  }

private:
  std::pair<double, double> coordinates(wr::GeoPoint p) const {
    return {wr::normalizeLongitude(p.longitude - request_.start.longitude) *
                longitudeScale_,
            (p.latitude - request_.start.latitude) * 60};
  }
  wr::GeoPoint position(double x, double y) const {
    return {
        std::clamp(request_.start.latitude + y / 60, -85., 85.),
        wr::normalizeLongitude(request_.start.longitude + x / longitudeScale_)};
  }
  wr::GeoPoint centre(unsigned index) const {
    return position(west_ + (index % width_ + .5) * spacing_,
                    south_ + (index / width_ + .5) * spacing_);
  }
  std::optional<unsigned> cellAt(wr::GeoPoint p) const {
    if (cost_.empty()) return {};
    const auto [x, y] = coordinates(p);
    const int xx = static_cast<int>(std::floor((x - west_) / spacing_));
    const int yy = static_cast<int>(std::floor((y - south_) / spacing_));
    if (xx < 0 || yy < 0 || xx >= static_cast<int>(width_) ||
        yy >= static_cast<int>(height_))
      return {};
    return static_cast<unsigned>(yy) * width_ + static_cast<unsigned>(xx);
  }
  void prepareField() {
    if (attempted_ || !provider_ || !options_.guidanceEnabled ||
        !provider_->supportsLandRejectionGuidance())
      return;
    attempted_ = true;
    const auto begun = std::chrono::steady_clock::now();
    const auto [east, north] = coordinates(request_.destination);
    const double length = std::hypot(east, north);
    const double padding = std::max(3., length * .3);
    west_ = std::min(0., east) - padding;
    south_ = std::min(0., north) - padding;
    const double spanX = std::abs(east) + 2 * padding,
                 spanY = std::abs(north) + 2 * padding;
    spacing_ = std::max(.25, length / 24);
    do {
      width_ = static_cast<unsigned>(std::ceil(spanX / spacing_));
      height_ = static_cast<unsigned>(std::ceil(spanY / spacing_));
      if (static_cast<std::uint64_t>(width_) * height_ <=
          options_.guidanceGridCells)
        break;
      spacing_ *= 1.2;
    } while (true);
    const unsigned count = width_ * height_;
    water_.assign(count, false);
    cost_.assign(count, std::numeric_limits<double>::infinity());
    for (unsigned i = 0; i < count; ++i) {
      if (request_.cancellation.cancelled() ||
          diagnostics_.hintQueries >= options_.maximumGuidanceQueries)
        break;
      ++diagnostics_.hintQueries;
      water_[i] = !provider_->pointForbidden(centre(i));
    }
    diagnostics_.guidanceCells = count;
    const auto goal = cellAt(request_.destination);
    if (goal && water_[*goal]) {
      using Entry = std::pair<double, unsigned>;
      std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
      cost_[*goal] = wr::distanceNm(centre(*goal), request_.destination);
      queue.emplace(cost_[*goal], *goal);
      while (!queue.empty() && !request_.cancellation.cancelled()) {
        const auto [cost, index] = queue.top();
        queue.pop();
        if (cost != cost_[index]) continue;
        const int x = static_cast<int>(index % width_),
                  y = static_cast<int>(index / width_);
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const int xx = x + dx, yy = y + dy;
            if ((!dx && !dy) || xx < 0 || yy < 0 ||
                xx >= static_cast<int>(width_) ||
                yy >= static_cast<int>(height_))
              continue;
            const auto next =
                static_cast<unsigned>(yy) * width_ + static_cast<unsigned>(xx);
            if (!water_[next]) continue;
            const double candidate =
                cost + wr::distanceNm(centre(index), centre(next));
            if (candidate >= cost_[next]) continue;
            if (diagnostics_.hintQueries >= options_.maximumGuidanceQueries)
              continue;
            ++diagnostics_.hintQueries;
            if (provider_->segmentForbidden(centre(index), centre(next), 0))
              continue;
            cost_[next] = candidate;
            queue.emplace(candidate, next);
          }
      }
    }
    diagnostics_.guidanceBuildSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begun)
            .count();
  }

  const wr::RoutingRequest& request_;
  const Options& options_;
  Diagnostics& diagnostics_;
  std::shared_ptr<const wr::LandAndBoundaryProvider> provider_;
  double longitudeScale_;
  std::map<std::pair<int, int>, double> clearance_;
  std::deque<std::pair<int, int>> insertionOrder_;
  bool attempted_{};
  double west_{}, south_{}, spacing_{};
  unsigned width_{}, height_{};
  std::vector<double> cost_;
  std::vector<bool> water_;
};
}  // namespace mim_coastal::detail
