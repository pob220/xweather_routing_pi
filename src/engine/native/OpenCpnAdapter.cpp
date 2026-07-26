#include "ModernNativeRoute.h"

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/datetime.h>
#include <wx/font.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ConstraintChecker.h"
#include "RouteMapOverlay.h"
#include "RoutingQualityPolicy.h"
#include "SunCalculator.h"
#include "WeatherDataProvider.h"
#include "supercpn/weather_routing/Engine.h"

namespace {
namespace wr = supercpn::weather_routing;

wxDateTime ToWx(wr::TimePoint time) {
  return wxDateTime(static_cast<time_t>(time.time_since_epoch().count()));
}

wr::TimePoint ToNative(const wxDateTime& time) {
  return wr::TimePoint{
      wr::Duration{static_cast<std::int64_t>(time.GetTicks())}};
}

bool Complete(wr::RoutingStatus status) {
  return status == wr::RoutingStatus::Complete ||
         status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
         status == wr::RoutingStatus::CompleteUsingGraphFallback;
}

struct OpenCpnSample {
  bool available{};
  double windFromWater{};
  double windSpeedWater{};
  double windFromGround{};
  double windSpeedGround{};
  double currentToward{};
  double currentSpeed{};
  double waveHeight{std::numeric_limits<double>::quiet_NaN()};
  double waveDirection{std::numeric_limits<double>::quiet_NaN()};
  double wavePeriod{std::numeric_limits<double>::quiet_NaN()};
  int dataMask{};
  wr::TimePoint sampleTime{};
};

struct OpenCpnWeatherCacheKey {
  std::int64_t time{};
  int latitude{};
  int longitude{};
  bool operator==(const OpenCpnWeatherCacheKey&) const = default;
};

struct OpenCpnWeatherCacheKeyHash {
  std::size_t operator()(const OpenCpnWeatherCacheKey& key) const noexcept {
    std::size_t value = std::hash<std::int64_t>{}(key.time);
    value ^= std::hash<int>{}(key.latitude) + 0x9e3779b9U + (value << 6U) +
             (value >> 2U);
    value ^= std::hash<int>{}(key.longitude) + 0x9e3779b9U + (value << 6U) +
             (value >> 2U);
    return value;
  }
};

struct OpenCpnSharedWeatherCache {
  std::mutex mutex;
  std::condition_variable ready;
  std::unordered_map<OpenCpnWeatherCacheKey, OpenCpnSample,
                     OpenCpnWeatherCacheKeyHash>
      samples;
  std::unordered_set<OpenCpnWeatherCacheKey, OpenCpnWeatherCacheKeyHash>
      inFlight;
};

std::shared_ptr<OpenCpnSharedWeatherCache> SharedWeatherCacheFor(
    const RouteMapConfiguration& configuration) {
  if (!configuration.DepartureTimeOptimizationCandidate ||
      configuration.DepartureTimeOptimizationGroupId.IsEmpty())
    return std::make_shared<OpenCpnSharedWeatherCache>();

  const std::string key =
      (configuration.DepartureTimeOptimizationGroupId +
       wxString::Format(
           ":grib%d:currents%d:climatology%d", configuration.UseGrib ? 1 : 0,
           configuration.Currents ? 1 : 0, configuration.ClimatologyType))
          .ToStdString();
  static std::mutex registryMutex;
  static std::map<std::string, std::shared_ptr<OpenCpnSharedWeatherCache> >
      registry;
  std::lock_guard<std::mutex> lock(registryMutex);
  const auto found = registry.find(key);
  if (found != registry.end()) return found->second;

  // Keep completed slices alive across bounded candidate batches.  A weak
  // registry loses the cache if one batch finishes before the next begins.
  constexpr std::size_t kRetainedDepartureGroups = 2;
  if (registry.size() >= kRetainedDepartureGroups)
    registry.erase(registry.begin());
  std::shared_ptr<OpenCpnSharedWeatherCache> cache =
      std::make_shared<OpenCpnSharedWeatherCache>();
  registry[key] = cache;
  return cache;
}

class OpenCpnWeatherProvider final : public wr::WeatherProvider {
public:
  OpenCpnWeatherProvider(RouteMapOverlay& overlay,
                         RouteMapConfiguration configuration)
      : overlay_(overlay),
        configuration_(std::move(configuration)),
        sharedCache_(SharedWeatherCacheFor(configuration_)) {}

  wr::ParameterCoverage windCoverage() const override {
    return {configuration_.UseGrib || configuration_.ClimatologyType >
                                          RouteMapConfiguration::CURRENTS_ONLY,
            {},
            {},
            {-180.0, -90.0, 180.0, 90.0},
            identity()};
  }
  wr::ParameterCoverage currentCoverage() const override {
    return {configuration_.Currents,
            {},
            {},
            {-180.0, -90.0, 180.0, 90.0},
            identity()};
  }
  wr::ParameterCoverage waveCoverage() const override {
    return {configuration_.UseGrib,
            {},
            {},
            {-180.0, -90.0, 180.0, 90.0},
            identity()};
  }

  wr::WindSample wind(wr::GeoPoint position,
                      wr::TimePoint time) const override {
    const OpenCpnSample sample = load(position, time);
    wr::WindSample result;
    if (!sample.available) return result;
    result.available = true;
    result.velocity = wr::speedDirectionToVector(sample.windSpeedWater,
                                                 sample.windFromWater + 180.0);
    result.metadata = metadata((sample.dataMask & Position::CLIMATOLOGY_WIND)
                                   ? wr::EnvironmentalSource::Climatology
                                   : wr::EnvironmentalSource::GribForecast,
                               sample.sampleTime);
    return result;
  }

  wr::CurrentSample current(wr::GeoPoint position,
                            wr::TimePoint time) const override {
    const OpenCpnSample sample = load(position, time);
    wr::CurrentSample result;
    if (!configuration_.Currents || !sample.available ||
        !(sample.dataMask &
          (Position::GRIB_CURRENT | Position::CLIMATOLOGY_CURRENT)))
      return result;
    result.available = true;
    result.velocity =
        wr::speedDirectionToVector(sample.currentSpeed, sample.currentToward);
    result.metadata =
        metadata((sample.dataMask & Position::CLIMATOLOGY_CURRENT)
                     ? wr::EnvironmentalSource::XtdCurrentPrediction
                     : wr::EnvironmentalSource::GribForecast,
                 sample.sampleTime);
    return result;
  }

  wr::WaveSample waves(wr::GeoPoint position,
                       wr::TimePoint time) const override {
    const OpenCpnSample sample = load(position, time);
    wr::WaveSample result;
    if (!std::isfinite(sample.waveHeight)) return result;
    result.available = true;
    result.significantHeightMetres = sample.waveHeight;
    result.directionFromDegrees = sample.waveDirection;
    result.periodSeconds = sample.wavePeriod;
    result.metadata =
        metadata(wr::EnvironmentalSource::GribForecast, sample.sampleTime);
    return result;
  }

  std::string identity() const override {
    return "OpenCPN GRIB/climatology timeline";
  }

  struct CacheDiagnostics {
    std::uint64_t calls{};
    std::uint64_t immediateHits{};
    std::uint64_t localHits{};
    std::uint64_t sharedHits{};
    std::uint64_t misses{};
    std::uint64_t waits{};
    std::uint64_t interpolationMicroseconds{};
  };

  CacheDiagnostics cacheDiagnostics() const {
    return {cacheCalls_,
            cacheImmediateHits_,
            cacheLocalHits_,
            cacheHits_,
            cacheMisses_,
            cacheWaits_,
            cacheInterpolationMicroseconds_};
  }

private:
  struct LocalCacheSlot {
    bool valid{};
    OpenCpnWeatherCacheKey key{};
    OpenCpnSample sample{};
  };

  static wr::EnvironmentalSourceMetadata metadata(
      wr::EnvironmentalSource source, wr::TimePoint time) {
    return {
        source, "OpenCPN weather services", "OpenCPN timeline", {}, time, {},
        {}};
  }

  OpenCpnSample load(wr::GeoPoint position, wr::TimePoint requested) const {
    ++cacheCalls_;
    // Fifteen-minute weather slices match final validation and avoid a full
    // copied GRIB grid for every sub-second timestamp encountered by graph
    // labels. A 0.01-degree spatial key is roughly 0.36 NM east/west in the
    // Irish Sea and 0.60 NM north/south. This is still 2.5 times finer in each
    // dimension than the 0.025-degree high-resolution Irish Sea fixture (and
    // materially finer than common forecast grids), while avoiding hundreds
    // of thousands of distinct sub-grid interpolations which contain no new
    // forecast information.
    constexpr std::int64_t kSliceSeconds = 15 * 60;
    constexpr double kSpatialSlicesPerDegree = 100.0;
    const auto seconds = requested.time_since_epoch().count();
    const auto quantizedSeconds =
        ((seconds + kSliceSeconds / 2) / kSliceSeconds) * kSliceSeconds;
    const OpenCpnWeatherCacheKey key{
        quantizedSeconds,
        static_cast<int>(
            std::llround(position.latitude * kSpatialSlicesPerDegree)),
        static_cast<int>(
            std::llround(position.longitude * kSpatialSlicesPerDegree))};
    // Environment resolution requests wind, current and waves consecutively
    // for the same quantised point/time. Each route owns its provider, so this
    // single-entry hot cache removes two mutex/hash lookups per snapshot
    // without changing the shared cache key or any interpolated value.
    if (lastSampleValid_ && lastSampleKey_ == key) {
      ++cacheImmediateHits_;
      return lastSample_;
    }
    // Search frontiers revisit nearby canonical weather buckets frequently.
    // Keep a small route-local direct-mapped cache in front of the shared
    // departure-family cache so parallel candidates do not serialize millions
    // of read-only hits on the shared mutex. A collision only loses a cache
    // entry; it cannot change the canonical value returned.
    const std::size_t localIndex =
        OpenCpnWeatherCacheKeyHash{}(key) & (localCache_.size() - 1U);
    LocalCacheSlot& local = localCache_[localIndex];
    if (local.valid && local.key == key) {
      ++cacheLocalHits_;
      lastSampleKey_ = key;
      lastSample_ = local.sample;
      lastSampleValid_ = true;
      return local.sample;
    }
    {
      std::unique_lock<std::mutex> lock(sharedCache_->mutex);
      for (;;) {
        const auto found = sharedCache_->samples.find(key);
        if (found != sharedCache_->samples.end()) {
          ++cacheHits_;
          local.valid = true;
          local.key = key;
          local.sample = found->second;
          lastSampleKey_ = key;
          lastSample_ = found->second;
          lastSampleValid_ = true;
          return found->second;
        }
        if (sharedCache_->inFlight.insert(key).second) {
          ++cacheMisses_;
          break;
        }
        ++cacheWaits_;
        sharedCache_->ready.wait(lock);
      }
    }
    const auto interpolationStarted = std::chrono::steady_clock::now();
    const auto publish = [&](const OpenCpnSample& sample) {
      cacheInterpolationMicroseconds_ +=
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - interpolationStarted)
              .count();
      {
        std::lock_guard<std::mutex> lock(sharedCache_->mutex);
        // A full-quality Irish Sea route can legitimately touch more than
        // 150k fine weather keys. Retaining the working set avoids repeatedly
        // interpolating evicted points; 500k entries remains a bounded cache
        // (roughly tens of MiB for this compact sample).
        if (sharedCache_->samples.size() >= 500000) {
          size_t erase = 50000;
          for (auto it = sharedCache_->samples.begin();
               it != sharedCache_->samples.end() && erase > 0;) {
            it = sharedCache_->samples.erase(it);
            --erase;
          }
        }
        sharedCache_->samples.emplace(key, sample);
        sharedCache_->inFlight.erase(key);
      }
      sharedCache_->ready.notify_all();
      local.valid = true;
      local.key = key;
      local.sample = sample;
      lastSampleKey_ = key;
      lastSample_ = sample;
      lastSampleValid_ = true;
      return sample;
    };

    const wr::TimePoint sampleTime{wr::Duration{quantizedSeconds}};
    // Interpolate at the key's canonical centre, not whichever fine-grained
    // search state happens to populate the bucket first. This makes shared
    // and retained-cache results independent of traversal, thread scheduling
    // and eviction order.
    const wr::GeoPoint samplePosition{key.latitude / kSpatialSlicesPerDegree,
                                      key.longitude / kSpatialSlicesPerDegree};
    RouteMapConfiguration configuration = configuration_;
    configuration.time = ToWx(sampleTime);
    configuration.grib = nullptr;
    configuration.grib_is_data_deficient = false;
    Shared_GribRecordSet frame;
    if (configuration.UseGrib) {
      if (!overlay_.AcquireGribTimelineFrame(configuration.time, frame)) {
        if (configuration.ClimatologyType <=
            RouteMapConfiguration::CURRENTS_ONLY)
          return publish({});
      } else {
        configuration.grib = frame.GetGribRecordSet();
      }
    }

    RoutePoint point(samplePosition.latitude, samplePosition.longitude);
    OpenCpnSample sample;
    sample.sampleTime = sampleTime;
    climatology_wind_atlas atlas{};
    sample.available = WeatherDataProvider::ReadWindAndCurrents(
        configuration, &point, sample.windFromGround, sample.windSpeedGround,
        sample.windFromWater, sample.windSpeedWater, sample.currentToward,
        sample.currentSpeed, atlas, sample.dataMask);
    if (sample.available) {
      sample.waveHeight = WeatherDataProvider::GetSwell(
          configuration, samplePosition.latitude, samplePosition.longitude);
      sample.waveDirection = WeatherDataProvider::GetWaveDirection(
          configuration, samplePosition.latitude, samplePosition.longitude);
      sample.wavePeriod = WeatherDataProvider::GetWavePeriod(
          configuration, samplePosition.latitude, samplePosition.longitude);
    }
    return publish(sample);
  }

  RouteMapOverlay& overlay_;
  RouteMapConfiguration configuration_;
  std::shared_ptr<OpenCpnSharedWeatherCache> sharedCache_;
  mutable std::uint64_t cacheCalls_{};
  mutable std::uint64_t cacheImmediateHits_{};
  mutable std::uint64_t cacheLocalHits_{};
  mutable std::uint64_t cacheHits_{};
  mutable std::uint64_t cacheMisses_{};
  mutable std::uint64_t cacheWaits_{};
  mutable std::uint64_t cacheInterpolationMicroseconds_{};
  mutable bool lastSampleValid_{};
  mutable OpenCpnWeatherCacheKey lastSampleKey_{};
  mutable OpenCpnSample lastSample_{};
  // Power-of-two size is required by the inexpensive mask in load().
  mutable std::vector<LocalCacheSlot> localCache_{32768U};
};

class OpenCpnPerformanceModel final : public wr::VesselPerformanceModel {
public:
  explicit OpenCpnPerformanceModel(RouteMapConfiguration configuration)
      : configuration_(std::move(configuration)) {}

  bool valid(std::string* reason) const override {
    const bool result =
        !configuration_.boat.Polars.empty() ||
        (configuration_.UseMotor && configuration_.MotorSpeed > 0.0);
    if (!result && reason) *reason = "no usable OpenCPN polar or motor speed";
    return result;
  }

  std::vector<wr::PerformanceCandidate> candidates(
      double tws, double twa, const wr::WaveSample& waves,
      wr::PropulsionMode previousMode,
      wr::Duration previousModeDuration) const override {
    return candidatesAt({configuration_.StartLat, configuration_.StartLon},
                        ToNative(configuration_.StartTime), tws, twa, waves,
                        previousMode, previousModeDuration);
  }

  wr::PerformanceCandidate evaluate(
      wr::PropulsionMode mode, wr::ProfileRole role,
      const std::string& identity, double tws, double twa,
      const wr::WaveSample& waves) const override {
    return evaluateAt({configuration_.StartLat, configuration_.StartLon},
                      ToNative(configuration_.StartTime), mode, role, identity,
                      tws, twa, waves);
  }

  std::vector<wr::PerformanceCandidate> candidatesAt(
      wr::GeoPoint position, wr::TimePoint time, double tws, double twa,
      const wr::WaveSample&, wr::PropulsionMode, wr::Duration) const override {
    std::vector<wr::PerformanceCandidate> result;
    for (std::size_t index = 0; index < configuration_.boat.Polars.size();
         ++index) {
      auto candidate = evaluatePolar(index, position, time, tws, twa);
      if (candidate.valid) result.push_back(std::move(candidate));
    }
    if (result.empty() && configuration_.UseMotor &&
        configuration_.MotorSpeed > 0.0) {
      wr::PerformanceCandidate motor;
      motor.valid = true;
      motor.mode = wr::PropulsionMode::Motor;
      motor.role = wr::ProfileRole::MotorOnly;
      motor.profileIdentity = "configured-motor";
      motor.speedThroughWaterKnots = configuration_.MotorSpeed;
      result.push_back(std::move(motor));
    }
    return result;
  }

  wr::PerformanceCandidate evaluateAt(wr::GeoPoint position, wr::TimePoint time,
                                      wr::PropulsionMode mode, wr::ProfileRole,
                                      const std::string& identity, double tws,
                                      double twa,
                                      const wr::WaveSample&) const override {
    if (mode == wr::PropulsionMode::Motor) {
      wr::PerformanceCandidate motor;
      motor.valid = configuration_.UseMotor && configuration_.MotorSpeed > 0.0;
      motor.mode = mode;
      motor.role = wr::ProfileRole::MotorOnly;
      motor.profileIdentity = identity.empty() ? "configured-motor" : identity;
      motor.speedThroughWaterKnots = configuration_.MotorSpeed;
      return motor;
    }
    std::size_t index = 0;
    if (identity.rfind("polar:", 0) == 0) {
      const auto end = identity.find(':', 6);
      try {
        index = static_cast<std::size_t>(std::stoul(identity.substr(
            6, end == std::string::npos ? std::string::npos : end - 6)));
      } catch (...) {
        return {};
      }
    } else {
      for (; index < configuration_.boat.Polars.size(); ++index)
        if (configuration_.boat.Polars[index].FileName.ToStdString() ==
            identity)
          break;
    }
    return evaluatePolar(index, position, time, tws, twa);
  }

private:
  wr::PerformanceCandidate evaluatePolar(std::size_t index,
                                         wr::GeoPoint position,
                                         wr::TimePoint time, double tws,
                                         double twa) const {
    wr::PerformanceCandidate result;
    if (index >= configuration_.boat.Polars.size()) return result;
    PolarSpeedStatus status = POLAR_SPEED_SUCCESS;
    double speed = configuration_.boat.Polars[index].Speed(
        twa, tws, &status, false, configuration_.OptimizeTacking);
    if (!std::isfinite(speed) || speed <= 0.0) return result;
    if (configuration_.UseMotor && speed < configuration_.MotorSpeedThreshold) {
      result.mode = wr::PropulsionMode::Motor;
      result.role = wr::ProfileRole::MotorOnly;
      speed = configuration_.MotorSpeed;
    } else {
      result.mode = wr::PropulsionMode::Sail;
      result.role = wr::ProfileRole::SailOnly;
      speed *= twa <= 90.0 ? configuration_.UpwindEfficiency
                           : configuration_.DownwindEfficiency;
      if (SunCalculator::GetInstance().GetDayLightStatus(
              position.latitude, position.longitude, ToWx(time)) ==
          DayLightStatus::Night)
        speed *= configuration_.NightCumulativeEfficiency;
    }
    result.valid = std::isfinite(speed) && speed > 0.0;
    result.sailPlan = static_cast<int>(index);
    result.profileIdentity =
        "polar:" + std::to_string(index) + ":" +
        configuration_.boat.Polars[index].FileName.ToStdString();
    result.speedThroughWaterKnots = speed;
    return result;
  }

  mutable RouteMapConfiguration configuration_;
};

class OpenCpnLandProvider final : public wr::LandAndBoundaryProvider {
public:
  OpenCpnLandProvider(RouteMapOverlay& overlay,
                      RouteMapConfiguration configuration)
      : overlay_(overlay), configuration_(std::move(configuration)) {}

  bool pointForbidden(wr::GeoPoint) const override {
    // Known start/destination points are allowed to egress from a configured
    // margin. Every actual segment is still chart-checked below and again by
    // independent dense final validation on the UI thread.
    return false;
  }

  bool segmentForbidden(wr::GeoPoint start, wr::GeoPoint end,
                        double margin) const override {
    return segmentForbiddenAt(start, end, ToNative(configuration_.time),
                              margin);
  }

  bool segmentForbiddenAt(wr::GeoPoint start, wr::GeoPoint end,
                          wr::TimePoint time, double margin) const override {
    // Speculative forward/reverse/graph edges use the fast shoreline. Every
    // deliverable candidate is batch-prepared and independently replayed
    // against authoritative OpenCPN charts below; a rejection can then invoke
    // full chart-aware propagation. Chart-checking exploratory endpoint fans
    // here repeats the expensive raster work for paths which are never
    // candidates.
    return segmentForbiddenAtImpl(start, end, time, margin,
                                  configuration_.UseChartSafetyForPropagation);
  }

  bool validationSegmentFromKnownSafeForbiddenAt(wr::GeoPoint start,
                                                 wr::GeoPoint end,
                                                 wr::TimePoint time,
                                                 double margin) const override {
    // Scouts only describe/prewarm a corridor and are never deliverable
    // routes. All production candidates, including those found with the fast
    // shoreline search, are replayed against authoritative OpenCPN charts.
    return segmentForbiddenAtImpl(start, end, time, margin,
                                  !configuration_.chart_safety_scout_preview);
  }

  void prepareValidationRoute(std::span<const wr::RouteLeg> legs,
                              double margin) const override {
    if (configuration_.chart_safety_scout_preview || legs.empty()) return;

    constexpr unsigned kMaximumPreparationRounds = 64;
    unsigned rounds = 0;
    unsigned missing = 0;
    for (; rounds < kMaximumPreparationRounds; ++rounds) {
      RouteMapConfiguration configuration = configuration_;
      configuration.UseChartSafetyForPropagation = true;
      configuration.SafetyMarginLand = std::max(0.0, margin);
      configuration.chart_safety_missing_tile_rejections = 0;
      for (const auto& leg : legs) {
        configuration.time = ToWx(leg.startTime);
        const double bearing = wr::initialBearingDegrees(leg.start, leg.end);
        (void)ConstraintChecker::CheckLandConstraint(
            configuration, leg.start.latitude, leg.start.longitude,
            leg.end.latitude, leg.end.longitude, bearing);
      }
      missing = configuration.chart_safety_missing_tile_rejections;
      if (missing == 0) break;
      if (!overlay_.AwaitChartSafetyData()) break;
    }
    wxLogMessage(
        "WR_VALIDATION_TILE_BATCH route=\"%s -> %s\" legs=%lu rounds=%u "
        "remaining_missing=%u authoritative_chart=1",
        configuration_.Start, configuration_.End,
        static_cast<unsigned long>(legs.size()), rounds + 1, missing);
  }

  bool validationFailureRequiresSearchEscalation() const override {
    return !configuration_.UseChartSafetyForPropagation;
  }

private:
  bool segmentForbiddenAtImpl(wr::GeoPoint start, wr::GeoPoint end,
                              wr::TimePoint time, double margin,
                              bool authoritativeChart) const {
    RouteMapConfiguration configuration = configuration_;
    configuration.time = ToWx(time);
    configuration.UseChartSafetyForPropagation = authoritativeChart;
    configuration.SafetyMarginLand = std::max(0.0, margin);
    if (!ConstraintChecker::CheckMaxCourseAngleConstraint(
            configuration, end.latitude, end.longitude) ||
        !ConstraintChecker::CheckMaxDivertedCourse(configuration, end.latitude,
                                                   end.longitude))
      return true;
    const double bearing = wr::initialBearingDegrees(start, end);
    bool safe = ConstraintChecker::CheckLandConstraint(
        configuration, start.latitude, start.longitude, end.latitude,
        end.longitude, bearing);
    if (configuration.chart_safety_missing_tile_rejections > 0) {
      if (!overlay_.AwaitChartSafetyData()) return true;
      configuration = configuration_;
      configuration.time = ToWx(time);
      configuration.UseChartSafetyForPropagation = authoritativeChart;
      configuration.SafetyMarginLand = std::max(0.0, margin);
      safe = ConstraintChecker::CheckLandConstraint(
          configuration, start.latitude, start.longitude, end.latitude,
          end.longitude, bearing);
    }
    if (!safe) return true;
    if (configuration.DetectBoundary &&
        RouteMap::ODFindClosestBoundaryLineCrossing) {
      RoutePoint point(start.latitude, start.longitude);
      if (point.EntersBoundary(end.latitude, end.longitude)) return true;
    }
    return !ConstraintChecker::CheckCycloneTrackConstraint(
        configuration, start.latitude, start.longitude, end.latitude,
        end.longitude);
  }

public:
  bool segmentFromKnownSafeForbidden(wr::GeoPoint start, wr::GeoPoint end,
                                     double margin) const override {
    return segmentForbidden(start, end, margin);
  }

  bool segmentFromKnownSafeForbiddenAt(wr::GeoPoint start, wr::GeoPoint end,
                                       wr::TimePoint time,
                                       double margin) const override {
    return segmentForbiddenAt(start, end, time, margin);
  }

  bool visualizationSegmentForbidden(wr::GeoPoint start, wr::GeoPoint end,
                                     double) const override {
    RouteMapConfiguration configuration = configuration_;
    configuration.UseChartSafetyForPropagation = false;
    const double bearing = wr::initialBearingDegrees(start, end);
    if (!ConstraintChecker::CheckLandConstraint(configuration, start.latitude,
                                                start.longitude, end.latitude,
                                                end.longitude, bearing))
      return true;
    if (configuration.DetectBoundary &&
        RouteMap::ODFindClosestBoundaryLineCrossing) {
      RoutePoint point(start.latitude, start.longitude);
      if (point.EntersBoundary(end.latitude, end.longitude)) return true;
    }
    return false;
  }

  double distanceToForbiddenNm(wr::GeoPoint point) const override {
    // The validator only needs to distinguish a point which is already clear
    // of the configured stand-off from one inside that buffer. A zero-length
    // OpenCPN chord has no raster samples and therefore cannot prove this.
    // Tiny radial spokes do exercise the authoritative route mask while
    // remaining much smaller than both the safety margin and a retained route
    // state. While inside the buffer, dense replay uses zero-margin chart
    // checks and may switch back to the full margin only after these probes
    // clear.
    auto pointClearAtConfiguredMargin = [&]() {
      RouteMapConfiguration configuration = configuration_;
      configuration.time = configuration.StartTime;
      configuration.UseChartSafetyForPropagation = true;
      configuration.SafetyMarginLand =
          std::max(0.0, configuration_.SafetyMarginLand);
      configuration.chart_safety_missing_tile_rejections = 0;
      bool clear = true;
      constexpr double kProbeLengthNm = 0.02;
      for (double bearing = 0.0; bearing < 360.0; bearing += 45.0) {
        const wr::GeoPoint end =
            wr::destinationPoint(point, bearing, kProbeLengthNm);
        if (!ConstraintChecker::CheckLandConstraint(
                configuration, point.latitude, point.longitude, end.latitude,
                end.longitude, bearing)) {
          clear = false;
          break;
        }
      }
      return std::pair{clear,
                       configuration.chart_safety_missing_tile_rejections};
    };

    auto [clear, missing] = pointClearAtConfiguredMargin();
    if (missing > 0 && overlay_.AwaitChartSafetyData())
      std::tie(clear, missing) = pointClearAtConfiguredMargin();
    return clear ? std::numeric_limits<double>::infinity() : 0.0;
  }
  std::string identity() const override {
    return "OpenCPN chart semantics, GSHHS fallback and exclusion boundaries";
  }

private:
  RouteMapOverlay& overlay_;
  RouteMapConfiguration configuration_;
};

wr::RoutingRequest BuildRequest(RouteMapOverlay& overlay,
                                const RouteMapConfiguration& configuration) {
  wr::RoutingRequest request;
  request.start = {configuration.StartLat, configuration.StartLon};
  request.destination = {configuration.EndLat, configuration.EndLon};
  request.departure = ToNative(configuration.StartTime);
  request.vessel.upwindEfficiency = 1.0;
  request.vessel.downwindEfficiency = 1.0;
  request.vessel.tackPenalty = wr::Duration{
      static_cast<std::int64_t>(std::llround(configuration.TackingTime))};
  request.vessel.gybePenalty = wr::Duration{
      static_cast<std::int64_t>(std::llround(configuration.JibingTime))};
  request.vessel.sailPlanChangePenalty = wr::Duration{static_cast<std::int64_t>(
      std::llround(configuration.SailPlanChangeTime))};
  request.vessel.propulsion.allowSailing = true;
  request.vessel.propulsion.allowMotor = configuration.UseMotor;
  request.vessel.propulsion.motorBelowSailingSpeedKnots =
      configuration.MotorSpeedThreshold;
  request.vessel.propulsion.configuredMotorSpeedKnots =
      configuration.MotorSpeed;

  request.environment.useWind = true;
  request.environment.useCurrent = configuration.Currents;
  request.environment.useWaves = configuration.UseGrib;
  request.environment.climatology =
      wr::ClimatologyFallbackPolicy::AllowWithWarning;
  request.environment.climatologyAcknowledged = true;
  request.environment.missingCurrent =
      wr::MissingCurrentPolicy::AllowAssumedZero;
  request.environment.zeroCurrentAcknowledged = true;
  request.environment.missingWaves = wr::MissingWavePolicy::AllowWithWarning;
  request.environment.missingWavesAcknowledged = true;

  if (configuration.MaxTrueWindKnots > 0.0)
    request.constraints.maximumTrueWindKnots = configuration.MaxTrueWindKnots;
  if (configuration.MaxApparentWindKnots > 0.0)
    request.constraints.maximumApparentWindKnots =
        configuration.MaxApparentWindKnots;
  if (configuration.MaxSwellMeters > 0.0)
    request.constraints.maximumWaveHeightMetres = configuration.MaxSwellMeters;
  if (configuration.WindVSCurrent > 0.0)
    request.constraints.maximumOpposingWindCurrent =
        configuration.WindVSCurrent;
  request.constraints.minimumTrueWindAngleDegrees = configuration.FromDegree;
  request.constraints.maximumTrueWindAngleDegrees = configuration.ToDegree;
  request.constraints.landSafetyMarginNm = configuration.SafetyMarginLand;
  request.constraints.maximumLatitudeDegrees = configuration.MaxLatitude;

  const double routeDistance =
      wr::distanceNm(request.start, request.destination);
  wxString previewValue;
  const bool preview =
      configuration.chart_safety_scout_preview ||
      (wxGetEnv("WR_ROUTING_PREVIEW", &previewValue) && previewValue != "0");
  const weather_routing::RoutingQualityPolicy quality =
      weather_routing::SelectRoutingQualityPolicy(
          preview, configuration.ByDegrees,
          static_cast<std::int64_t>(std::llround(configuration.DeltaTime)));
  const std::int64_t baseStep = quality.time_step_seconds;
  request.options.timeStep = wr::Duration{quality.time_step_seconds};
  request.options.minimumTimeStep = wr::Duration{10 * 60};
  request.options.headingStepDegrees = quality.heading_step_degrees;
  request.options.refinedHeadingStepDegrees =
      quality.refined_heading_step_degrees;
  request.options.maximumSearchAngleDegrees = configuration.MaxSearchAngle;
  request.options.destinationToleranceNm = 0.35;
  const double nominalDistance =
      std::max(0.5, configuration.MotorSpeed > 0.0
                        ? configuration.MotorSpeed * baseStep / 10800.0
                        : baseStep / 1800.0);
  request.options.spatialCellNm = std::clamp(nominalDistance, 1.0, 5.0);
  request.options.labelsPerCell = quality.labels_per_cell;
  request.options.adaptiveTimeStep = true;
  request.options.adaptiveHeadings = true;
  request.options.adaptiveFrontierDensity = true;
  request.options.useReverseRecovery =
      configuration.UseReverseReachabilityRecovery;
  request.options.useGraphFallback = true;
  request.options.retryStages = quality.retry_stages;
  request.options.reverseLayers = static_cast<unsigned>(
      std::max(8, configuration.ReverseReachabilitySearchBackIsochrones));
  request.options.reverseHorizon = wr::Duration{static_cast<std::int64_t>(
      std::max(24.0, configuration.ReverseReachabilityHorizonHours) * 3600.0)};
  request.options.graphCorridorWidthNm =
      std::max(40.0, wr::distanceNm(request.start, request.destination) * 0.45);
  request.options.graphHeadingStepDegrees =
      request.options.refinedHeadingStepDegrees;
  // With currents enabled there is no declared upper bound on favourable COG,
  // so zero deliberately selects Dijkstra instead of an inadmissible A* bound.
  request.options.heuristicMaximumSpeedKnots = 0.0;
  request.options.preserveRouteFamilies = quality.preserve_route_families;

  const double scale = std::clamp(routeDistance / 100.0, 0.6, 4.0);
  request.limits.maximumGeneratedStates =
      static_cast<std::uint64_t>(900000.0 * scale);
  request.limits.maximumRetainedStates =
      static_cast<std::uint64_t>(70000.0 * scale);
  request.limits.maximumGraphLabels =
      static_cast<std::uint64_t>(180000.0 * scale);
  request.limits.maximumRouteDuration = wr::Duration{30 * 24 * 3600};
  request.limits.maximumExplorationDistanceNm =
      std::max(500.0, routeDistance * 5.0);
  request.cancellation = wr::CancellationToken(overlay.CancellationFlag());
  request.progress = [&overlay](const wr::RoutingProgressUpdate& progress) {
    overlay.SetModernNativeProgress(progress);
  };
  return request;
}

}  // namespace

bool ModernNativeRouteEnabled(const RouteMapConfiguration& configuration) {
  wxString legacyValue;
  const bool forceLegacy =
      wxGetEnv("WR_USE_LEGACY_ENGINE", &legacyValue) && legacyValue != "0";
  const bool cumulativeClimatology =
      configuration.ClimatologyType == RouteMapConfiguration::CUMULATIVE_MAP ||
      configuration.ClimatologyType ==
          RouteMapConfiguration::CUMULATIVE_MINUS_CALMS;
  return !forceLegacy && !cumulativeClimatology &&
         configuration.RouteGUID.IsEmpty();
}

bool RunModernNativeRoute(RouteMapOverlay& overlay, wxString& error) {
  const auto started = std::chrono::steady_clock::now();
  const RouteMapConfiguration configuration = overlay.GetConfiguration();
  auto weather =
      std::make_shared<OpenCpnWeatherProvider>(overlay, configuration);
  auto land = std::make_shared<OpenCpnLandProvider>(overlay, configuration);
  auto performance = std::make_shared<OpenCpnPerformanceModel>(configuration);
  wr::RoutingEnvironment environment;
  environment.grib = weather;
  environment.landAndBoundaries = land;
  environment.performance = performance;
  environment.memberIdentity = "OpenCPN native deterministic";
  const wr::RoutingRequest request = BuildRequest(overlay, configuration);
  wr::RoutingEngine engine;
  const wr::RoutingResult result = engine.route(request, environment);
  const OpenCpnWeatherProvider::CacheDiagnostics weatherCache =
      weather->cacheDiagnostics();
  const auto elapsedMilliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count();
  const wxString status = wxString::FromUTF8(wr::toString(result.status));
  const wxString solver = wxString::FromUTF8(wr::toString(result.solverPath));
  wxLogMessage(
      "WR_MODERN_NATIVE_SUMMARY route=\"%s -> %s\" status=%s solver=%s "
      "elapsed_ms=%lld legs=%llu generated=%llu retained=%llu "
      "graph_labels=%llu wait_states=%llu land_checks=%llu "
      "land_rejections=%llu constraint_rejections=%llu "
      "validation_samples=%llu closest_nm=%.3f",
      configuration.Start, configuration.End, status, solver,
      static_cast<long long>(elapsedMilliseconds),
      static_cast<unsigned long long>(result.legs.size()),
      static_cast<unsigned long long>(result.diagnostics.generatedStates),
      static_cast<unsigned long long>(result.diagnostics.retainedStates),
      static_cast<unsigned long long>(result.diagnostics.graphLabels),
      static_cast<unsigned long long>(result.diagnostics.waitStates),
      static_cast<unsigned long long>(result.diagnostics.landChecks),
      static_cast<unsigned long long>(result.diagnostics.landRejections),
      static_cast<unsigned long long>(result.diagnostics.constraintRejections),
      static_cast<unsigned long long>(result.diagnostics.validationSamples),
      result.diagnostics.closestApproachNm);
  wxLogMessage(
      "WR_MODERN_WEATHER_CACHE route=\"%s -> %s\" calls=%llu "
      "immediate_hits=%llu local_hits=%llu shared_hits=%llu misses=%llu "
      "waits=%llu "
      "interpolation_us=%llu",
      configuration.Start, configuration.End,
      static_cast<unsigned long long>(weatherCache.calls),
      static_cast<unsigned long long>(weatherCache.immediateHits),
      static_cast<unsigned long long>(weatherCache.localHits),
      static_cast<unsigned long long>(weatherCache.sharedHits),
      static_cast<unsigned long long>(weatherCache.misses),
      static_cast<unsigned long long>(weatherCache.waits),
      static_cast<unsigned long long>(weatherCache.interpolationMicroseconds));
  for (const auto& reason : result.diagnostics.stageStopReasons)
    wxLogMessage("WR_MODERN_NATIVE_STAGE route=\"%s -> %s\" %s",
                 configuration.Start, configuration.End,
                 wxString::FromUTF8(reason));
  overlay.InstallModernNativeResult(result);
  if (!Complete(result.status)) error = wxString::FromUTF8(result.message);
  return Complete(result.status);
}
