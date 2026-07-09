# Weather Routing Headless Scenarios

This is a developer/test entry point for running Weather Routing scenarios
without driving the Weather Routing GUI. It currently uses the existing
`WeatherRouting` / `RouteMapOverlay` computation path; it does not introduce a
second routing algorithm.

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
cd /home/paul/src/OpenCPN
WR_HEADLESS_SCENARIO="$PWD/plugins/weather_routing_pi/testdata/scenarios/holyhead_dunlaoghaire.json" \
WR_HEADLESS_OUTPUT=/tmp/wr_result.json \
OPENCPN_PLUGIN_DIRS="$PWD/build/plugins/weather_routing_pi:/usr/lib/opencpn:/usr/lib64/opencpn" \
./build/opencpn
```

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

## Limitations

This runner is a first-class wrapper around the current headless test path, not
yet a standalone engine library. The next refactor stages should move request,
result, execution, and environment adapter boundaries out of `WeatherRouting`
incrementally while keeping the GUI and headless runner on the same route
algorithm.
