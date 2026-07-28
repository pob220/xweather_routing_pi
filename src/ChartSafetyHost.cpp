/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "ChartSafetyHost.h"

#include <cstring>

#include <wx/log.h>

#include "ChartSafetyCache.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

void* ResolveHostSymbol(const char* name) {
#ifdef _WIN32
  HMODULE process = GetModuleHandle(nullptr);
  return process ? reinterpret_cast<void*>(GetProcAddress(process, name))
                 : nullptr;
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

template <typename T>
T Resolve(const char* name) {
  return reinterpret_cast<T>(ResolveHostSymbol(name));
}

using RegisterFn = bool (*)(
    const PlugInSegmentSafetyTileCacheCallbacks*);
using IdentityFn = bool (*)(char*, int);
using CheckFn = bool (*)(double, double, double, double,
                         const PlugInSegmentSafetyOptions*,
                         PlugInSegmentSafetyResult*);
using SnapshotFn = bool (*)(double, double, double, double, int, int,
                            const PlugInSegmentSafetyOptions*,
                            PlugInSegmentSafetyResult*);
using SegmentMaskFn = bool (*)(double, double, double, double, double,
                               const PlugInSegmentSafetyOptions*,
                               PlugInSegmentSafetyResult*);
using PolylineMaskFn = bool (*)(
    const double*, const double*, const int*, int, double, int,
    const PlugInSegmentSafetyOptions*, PlugInSegmentSafetyResult*);
using ServiceFn = bool (*)(int, int,
                           PlugInSegmentSafetyRequestServiceResult*);
using ReleaseFn = void (*)();

struct HostFunctions {
  RegisterFn register_cache{nullptr};
  IdentityFn get_identity{nullptr};
  CheckFn check{nullptr};
  SnapshotFn snapshot{nullptr};
  SegmentMaskFn segment_mask{nullptr};
  PolylineMaskFn polyline_mask{nullptr};
  ServiceFn service{nullptr};
  ReleaseFn release{nullptr};
  bool available{false};
  bool registered{false};
};

HostFunctions g_host;
weather_routing::ChartSafetyCache* g_cache = nullptr;

}  // namespace

namespace weather_routing {
namespace chart_safety_host {

bool Initialize(ChartSafetyCache* cache) {
  Shutdown();
  g_cache = cache;
  g_host.register_cache = Resolve<RegisterFn>(
      "PlugIn_RegisterSegmentSafetyTileCache");
  g_host.get_identity = Resolve<IdentityFn>(
      "PlugIn_GetSegmentSafetyChartIdentity");
  g_host.check = Resolve<CheckFn>("PlugIn_CheckSegmentSafety");
  g_host.snapshot = Resolve<SnapshotFn>(
      "PlugIn_PrewarmSegmentSafetyHazardSnapshot");
  g_host.segment_mask = Resolve<SegmentMaskFn>(
      "PlugIn_PrewarmSegmentSafetyRouteMaskForSegment");
  g_host.polyline_mask = Resolve<PolylineMaskFn>(
      "PlugIn_PrewarmSegmentSafetyRouteMaskForPolylinesWithTileHalo");
  g_host.service = Resolve<ServiceFn>(
      "PlugIn_ServicePendingSegmentSafetyRequests");
  g_host.release = Resolve<ReleaseFn>(
      "PlugIn_ReleaseSegmentSafetyRouteMaskPins");

  g_host.available =
      cache && g_host.register_cache && g_host.get_identity && g_host.check &&
      g_host.snapshot && g_host.segment_mask && g_host.polyline_mask &&
      g_host.service && g_host.release;
  if (!g_host.available) return false;

  PlugInSegmentSafetyTileCacheCallbacks callbacks = {};
  callbacks.struct_size = sizeof(callbacks);
  callbacks.context = cache;
  callbacks.lookup = &ChartSafetyCache::LookupCallback;
  callbacks.store = &ChartSafetyCache::StoreCallback;
  callbacks.identity_changed = &ChartSafetyCache::IdentityCallback;
  if (!g_host.register_cache(&callbacks)) {
    g_host.available = false;
    return false;
  }
  g_host.registered = true;

  char identity[4096] = {};
  if (!g_host.get_identity(identity, sizeof(identity)) || !identity[0]) {
    Shutdown();
    return false;
  }
  cache->SetIdentity(identity);
  return true;
}

void Shutdown() {
  if (g_host.registered && g_host.register_cache)
    g_host.register_cache(nullptr);
  g_host = HostFunctions();
  g_cache = nullptr;
}

bool Available() { return g_host.available; }

std::string Status() {
  return Available()
             ? "Full chart-aware safety available; plugin RAM and disk cache "
               "active."
             : "Enhanced chart-safety host capability is unavailable.";
}

bool FlushCache() {
  if (!g_cache) return true;
  const bool result = g_cache->Flush(false);
  const ChartSafetyCacheStats stats = g_cache->Stats();
  wxLogMessage(
      "WR_PLUGIN_CHART_CACHE flush_ok=%d ram_hits=%llu disk_hits=%llu "
      "misses=%llu stores=%llu evictions=%llu ram_entries=%llu "
      "ram_mib=%.1f budget_mib=%.1f disk_entries=%llu "
      "disk_read_mib=%.1f disk_written_mib=%.1f disk_file_mib=%.1f "
      "dirty=%llu flushes=%llu",
      result ? 1 : 0, static_cast<unsigned long long>(stats.ram_hits),
      static_cast<unsigned long long>(stats.disk_hits),
      static_cast<unsigned long long>(stats.misses),
      static_cast<unsigned long long>(stats.stores),
      static_cast<unsigned long long>(stats.evictions),
      static_cast<unsigned long long>(stats.ram_entries),
      stats.ram_bytes / (1024.0 * 1024.0),
      stats.ram_budget_bytes / (1024.0 * 1024.0),
      static_cast<unsigned long long>(stats.disk_entries),
      stats.disk_bytes_read / (1024.0 * 1024.0),
      stats.disk_bytes_written / (1024.0 * 1024.0),
      stats.disk_file_bytes / (1024.0 * 1024.0),
      static_cast<unsigned long long>(stats.dirty_entries),
      static_cast<unsigned long long>(stats.flushes));
  return result;
}

bool CheckSegment(double lat1, double lon1, double lat2, double lon2,
                  const PlugInSegmentSafetyOptions* options,
                  PlugInSegmentSafetyResult* result) {
  return g_host.available &&
         g_host.check(lat1, lon1, lat2, lon2, options, result);
}

bool PrewarmHazardSnapshot(double min_lat, double min_lon, double max_lat,
                           double max_lon, int enable_fast_path,
                           int shadow_compare,
                           const PlugInSegmentSafetyOptions* options,
                           PlugInSegmentSafetyResult* result) {
  return g_host.available &&
         g_host.snapshot(min_lat, min_lon, max_lat, max_lon, enable_fast_path,
                         shadow_compare, options, result);
}

bool PrewarmRouteMaskForSegment(
    double lat1, double lon1, double lat2, double lon2,
    double corridor_margin_nm, const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result) {
  return g_host.available &&
         g_host.segment_mask(lat1, lon1, lat2, lon2, corridor_margin_nm,
                             options, result);
}

bool PrewarmRouteMaskForPolylinesWithTileHalo(
    const double* latitudes, const double* longitudes,
    const int* point_counts, int polyline_count, double corridor_margin_nm,
    int fine_tile_halo, const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result) {
  return g_host.available &&
         g_host.polyline_mask(latitudes, longitudes, point_counts,
                              polyline_count, corridor_margin_nm,
                              fine_tile_halo, options, result);
}

bool ServicePendingRequests(
    int max_requests, int max_milliseconds,
    PlugInSegmentSafetyRequestServiceResult* result) {
  return g_host.available &&
         g_host.service(max_requests, max_milliseconds, result);
}

void ReleaseRouteMaskPins() {
  if (g_host.available) g_host.release();
}

}  // namespace chart_safety_host
}  // namespace weather_routing
