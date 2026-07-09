# Weather Routing Headless Scenarios

This is a developer/test entry point for running Weather Routing scenarios
without driving the Weather Routing GUI. It currently uses the existing
`WeatherRouting` / `RouteMapOverlay` computation path; it does not introduce a
second routing algorithm.

## Build Prerequisites

Build OpenCPN and the Weather Routing plugin before running a scenario:

```sh
cd ~/src/OpenCPN/build
cmake --build . --target opencpn weather_routing_pi -j"$(nproc)"
```

Run from the OpenCPN source tree so relative plugin paths resolve as expected:

```sh
cd ~/src/OpenCPN
```

If another OpenCPN instance is running, headless runs may exit through the
single-instance IPC path. Stop the GUI instance before running scenario tests.

## Runtime Assumptions

The scenario runner uses the same Weather Routing plugin state and OpenCPN
services as the GUI path. It assumes:

- the Weather Routing plugin can be loaded from `OPENCPN_PLUGIN_DIRS`;
- suitable GRIB/weather data is already available in the OpenCPN profile;
- the requested time range is covered by the loaded GRIB data;
- the selected/default polar and boat settings are usable;
- chart-backed safety tests require the configured OpenCPN chart database;
- chart-backed safety uses OpenCPN core/plugin APIs, not plugin-side chart
  parsing.

The scenario file does not currently name a GRIB file or polar file. Those are
still supplied by the existing OpenCPN/Weather Routing configuration.

## Environment Variables

`WR_HEADLESS_SCENARIO` points to a JSON scenario file.

`WR_HEADLESS_OUTPUT` optionally points to the JSON result file. The runner writes
an initial `running` result when the scenario starts and overwrites it with the
final result when the run completes or fails.

The existing `WR_HEADLESS_ROUTE_TEST` path remains supported. If
`WR_HEADLESS_SCENARIO` is set without `WR_HEADLESS_ROUTE_TEST`, the plugin still
starts the headless runner.

Example:

```sh
cd ~/src/OpenCPN

WR_HEADLESS_SCENARIO="$PWD/plugins/weather_routing_pi/testdata/scenarios/holyhead_dunlaoghaire.json" \
WR_HEADLESS_OUTPUT=/tmp/wr_result.json \
OPENCPN_PLUGIN_DIRS="$PWD/build/plugins/weather_routing_pi:/usr/lib/opencpn:/usr/lib64/opencpn" \
./build/opencpn

cat /tmp/wr_result.json

rg "WR_HEADLESS|FINAL_ROUTE_SAFETY|WeatherRouting propagation summary|WR_GRID_THREAD_VIOLATION" \
  ~/.opencpn/opencpn.log | tail -200
```

## Verifying Plugin Load

The log should show the Weather Routing plugin loaded from the build tree, for
example:

```text
PluginLoader: Loading PlugIn: ~/src/OpenCPN/build/plugins/weather_routing_pi/libweather_routing_pi.so
WR_HEADLESS_ROUTE_TEST timer_scheduled ...
WR_HEADLESS_SCENARIO loaded ...
```

If the log shows only `/usr/lib/opencpn/libweather_routing_pi.so`, the test is
not using the just-built plugin. Check `OPENCPN_PLUGIN_DIRS`.

If the process exits immediately with an IPC warning, another OpenCPN instance
or stale IPC socket is probably blocking startup.

## Inspecting Logs

Useful log filters:

```sh
rg "WR_HEADLESS|FINAL_ROUTE_SAFETY|WeatherRouting propagation summary|WR_GRID_THREAD_VIOLATION" \
  ~/.opencpn/opencpn.log | tail -200

rg "WR_GRID|WR_ROUTE_MASK|WR_CERT_SAFE_CACHE|chart_api_calls_during_query" \
  ~/.opencpn/opencpn.log | tail -200
```

For chart-backed safety, worker segment queries should not call chart APIs.
Worker query log lines should show `chart_api_calls_during_query=0` when
`worker_thread_query_without_chart_api=1`.

## Scenario Schema

The first milestone supports:

- `schemaVersion`
- `name`
- `start.name`, `start.lat`, `start.lon`
- `end.name`, `end.lat`, `end.lon`
- `startTime`, ISO UTC
- `departureOptimization.enabled`
- `departureOptimization.beforeMinutes`
- `departureOptimization.afterMinutes`
- `departureOptimization.stepMinutes`
- `environment.useCurrents`
- `safety.mode`: `none`, `gshhs`, or `chart`
- `safety.enforce`
- `safety.landMarginNm`
- `safety.minimumDepthM`
- `safety.persistentCertifiedCacheEnabled`

`minimumDepthM` is parsed and retained in the scenario DTO. It is only applied
when the current Weather Routing/OpenCPN route safety configuration exposes a
matching option.

## Result Schema

The result file contains:

- `schemaVersion`
- `scenario`
- `status`
- `failureReason`
- `candidates[]`
  - `departure`
  - `state`
  - `eta`
  - `elapsed`
  - `distanceNm`
  - `finalSafety`
  - `failureReason`
  - `offsetMinutes`
- `diagnostics`

Diagnostics are intentionally sparse in this first milestone. Fields are omitted
when the existing computation path does not expose them cleanly.

Example result:

```json
{
  "schemaVersion": 1,
  "scenario": "Holyhead to Dun Laoghaire",
  "status": "complete",
  "candidates": [
    {
      "departure": "2026-07-10T06:00:00Z",
      "state": "complete",
      "eta": "2026-07-10T13:51:39Z",
      "elapsed": 28299,
      "distanceNm": 54.52,
      "finalSafety": "pass",
      "offsetMinutes": 0
    }
  ],
  "diagnostics": {}
}
```

Failure results still write JSON:

```json
{
  "schemaVersion": 1,
  "scenario": "Holyhead to Dun Laoghaire",
  "status": "failed",
  "failureReason": "no_completed_routes",
  "candidates": [
    {
      "departure": "2026-07-10T06:00:00Z",
      "state": "failed",
      "finalSafety": "fail",
      "failureReason": "No reachable route points",
      "offsetMinutes": 0
    }
  ],
  "diagnostics": {}
}
```

## Known Failure Modes

- `No reachable route points`: the existing routing engine failed to build a
  route under the active weather, polar, constraints, and safety settings.
- `Final route did not reach destination`: propagation produced geometry that
  could not be accepted as a completed destination route.
- `Chart safety grid data unavailable...`: chart-backed safety needed cells or
  masks that were not available after prewarm/retry.
- Immediate startup/exit with IPC warnings: another OpenCPN instance or stale
  IPC state is present.
- Empty or missing result file: the plugin did not load, the scenario did not
  parse, or OpenCPN exited before the headless timer fired.
- `WR_GRID_THREAD_VIOLATION`: unsafe chart API access from a worker thread; this
  is a correctness bug.

## Comparing Safety Modes

Edit the scenario `safety` block to compare modes:

```json
"safety": {
  "mode": "none",
  "enforce": false,
  "landMarginNm": 0.0,
  "minimumDepthM": 0,
  "persistentCertifiedCacheEnabled": false
}
```

```json
"safety": {
  "mode": "gshhs",
  "enforce": false,
  "landMarginNm": 0.0,
  "minimumDepthM": 0,
  "persistentCertifiedCacheEnabled": false
}
```

```json
"safety": {
  "mode": "chart",
  "enforce": true,
  "landMarginNm": 0.0,
  "minimumDepthM": 0,
  "persistentCertifiedCacheEnabled": true
}
```

Run the same scenario repeatedly with
`persistentCertifiedCacheEnabled` set to `false`, then `true`, then `true`
again. Compare log counters such as `fineTilesBuilt`,
`WR_CERT_SAFE_CACHE`, `persistent_cache_used_in_query`, and final validation
results.

Persistent certified safe-area cache entries are an optimisation only. Completed
routes still require final route safety validation.

## Exit Behaviour

On normal completion the process should write the result JSON and exit. In a
successful headless run the log normally ends with:

```text
WR_HEADLESS_ROUTE_TEST end ...
WR_HEADLESS_ROUTE_TEST process_exit code=0
```

If OpenCPN remains running after the headless result has been written, inspect
the shutdown log for plugin teardown or IPC messages. Do not treat a stale
process as a successful test until the result JSON and expected `WR_HEADLESS`
log lines have been inspected.

## Limitations

This runner is a first-class wrapper around the current headless test path, not
yet a standalone engine library. The next refactor stages should move request,
result, execution, and environment adapter boundaries out of `WeatherRouting`
incrementally while keeping the GUI and headless runner on the same route
algorithm.
