# xWeatherRouting improved UX development

This repository's default `develop` branch is the **improved UX development
2.x line**, currently **2.11.1**. It provides the passage-planning interface,
incorporated GRIB loader and generator, bundled Climatology, and separate
preferences and user data.

| Line | Current version | Source |
| --- | --- | --- |
| Weather Routing production | **1.44.1** | [pob220/weather_routing_pi — master](https://github.com/pob220/weather_routing_pi/tree/master) |
| xWeatherRouting improved UX development | **2.11.1** | [pob220/xweather_routing_pi — develop](https://github.com/pob220/xweather_routing_pi/tree/develop) |

[Download the 2.11.1 developer test packages](https://github.com/pob220/xweather_routing_pi/releases/tag/developer-test-2.11.1-20261124)
for Apple Silicon, Debian 12, Debian 13, Windows x64 and 32-bit OpenCPN on
64-bit Windows. Match the package to the OpenCPN application architecture and,
on Windows x64, its wxWidgets version. Import the intact `.tar.gz` through
OpenCPN's Plugin Manager.

Production 1.x retains the established Weather Routing interface and uses
external GRIB or xGRIB and Climatology providers. Its 1.44.1 integration into
Rick's `master` is tracked in [PR #740](https://github.com/rgleason/weather_routing_pi/pull/740).
The development packages are a separate line from the production catalogue.

Both lines include Meridian as an optional routing method. Ordinary GSHHG
shoreline routing works on stock OpenCPN; chart-backed depth and hazard checks
require a compatible chart-safety host. See the
[2.x development overview](docs/v2/README.md), [Meridian](docs/MERIDIAN.md) and
[routing settings](docs/quick-routing.md).

The optional hardened-OpenCPN planning-provider boundary is documented in
[docs/external_control_provider_preview_b.md](docs/external_control_provider_preview_b.md).
Stock OpenCPN remains supported through the unchanged plug-in API 1.21.

The development frontend shares routing methods and safety infrastructure with
production. Its planner groups route, boat and weather preparation, exposes
readiness checks and recovery actions, and retains the existing specialist
analysis and export tools.

The plugin currently provides:

- consistent wave-height checks and coverage warnings across routing engines,
  optional required wave coverage, and explicit anchoring waits with locations
  and durations in the route report;
- threshold-based motoring when sailing performance is insufficient, bounded
  polar wind policies, reversible route visibility, explicit result clearing
  and selected/all/manual departure display modes;
- a read-only **Check this route** action in the OpenCPN route context menu
  (v1.25). It reports land crossings, drying areas, charted hazards, land
  clearance, optional minimum charted depth and unavailable chart/depth data.
  Findings appear in a per-leg report and as temporary chart markers. Click
  a marker to select its report entry, or select an entry and choose
  **Show selected finding**. **Recheck** uses the latest route and chart state;
  closing the report removes its markers. Cancellation leaves an explicitly
  partial report. The checked route and its waypoints are never changed;
- deterministic adaptive forward isochrones, reverse recovery and
  time-dependent graph fallback;
- preservation of useful suboptimal lineages for difficult coastal routes;
- departure-time optimisation with independently isolated workers;
- planned-arrival routing, including determination of the required departure
  time;
- UTC routing internally with optional IANA local-time display in the UI;
- dense independent route validation and standard GSHHS land checks;
- optional enhanced chart-backed hazard checks when the OpenCPN host exposes
  the dynamically detected experimental service.

The same binary loads on an unmodified stock OpenCPN host. Stock OpenCPN does
not expose the optional chart-backed service, so Weather Routing disables
those two controls and continues to use the standard GSHHS checks.
There is no direct enhanced-core symbol dependency.

Route checking also requires the compatible chart-safety host. It checks the
rhumb-line legs between the route's existing waypoints using authoritative
chart queries, independently of the routing engines. Depth is at chart datum;
entering zero disables depth checks, and the report states this explicitly.
Android retains the established Plan / Routes / Results / Tools workspace.
Tools → Route management → Check this route opens the same read-only checker
through a touch route picker, with stacked settings, scrollable findings and
persistent Close/Back actions. Stock OpenCPN Android lacks the optional chart
safety service and reports that checking is unavailable; ordinary routing and
its GSHHG land checks remain available.
The checker does not evaluate traffic rules, restricted-area conditions,
bridge clearance or tide height. Reports become outdated when the route,
chart selection or chart identity changes. Version 1.27 pairs the existing
plugin with the optional geometry-based core improvements described in its
release notes. Real S-63 cells still require separate qualification; this
release does not claim a new S-63 provider integration.

Fresh installations leave both optional chart/depth controls unchecked.
Users of an enhanced OpenCPN host can opt in; explicit choices made by
existing users are preserved. Version 2 uses `/PlugIns/xWeatherRouting2`
preferences and the separate `plugins/xweather_routing_v2` user-data directory.
Supported feature upgrades preserve customized boats and polars.

## Building

For a clean standalone build against the vendored stock OpenCPN 1.21 API:

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEATHER_ROUTING_STANDALONE_API=ON \
  -DWEATHER_ROUTING_XWEATHER_IDENTITY=ON \
  -DOCPN_BUILD_TEST=OFF
cmake --build build-release --parallel
cmake --build build-release --target package
```

Use `WEATHER_ROUTING_XWEATHER_IDENTITY=ON` for this development plugin. The
production source and its standard identity are maintained separately above.

Keep release packaging in a build directory where tests are disabled. This
prevents test-only GoogleTest libraries from being included by older
packaging infrastructure.

## Testing

For real-GRIB smoke tests in a separate OpenCPN profile, see the
[headless testing kit](tools/headless-testing/). It includes a Python runner,
timing extractor, example scenario and setup instructions.

```sh
cmake -S . -B build-test -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEATHER_ROUTING_STANDALONE_API=ON \
  -DWEATHER_ROUTING_XWEATHER_IDENTITY=ON \
  -DOCPN_BUILD_TEST=ON
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure --parallel
```

The native engine, deterministic resource policies, planned-arrival planner,
timezone lifecycle, route validation, chart-cache data structures and
supporting geometry are covered by the test suite. Stock-host compatibility
must also be checked by loading the clean package in an unmodified OpenCPN
5.14 or later installation.

Further architecture and validation details are in
[`docs/modern_native_engine.md`](docs/modern_native_engine.md) and
[`docs/chart_safety_cache.md`](docs/chart_safety_cache.md).

For the wxWidgets 3.2 and 3.3.4 build variants and matching Windows x64
packages, see [`docs/release-1.35.0.md`](docs/release-1.35.0.md).

## Status

The development package uses the separate identity
`xweather_routing_pi` / xWeatherRouting. Version 2.11.1 is published as the
developer test linked above. The production line is WeatherRouting 1.44.1,
using `weather_routing_pi`; publication to the production catalogue is a
separate release step.

## Licence and acknowledgement

The plugin is GPL v3 or later. The original Weather Routing plugin was written
by Sean D'Epagnier and has benefited from many OpenCPN contributors,
translators and testers. This integration preserves that lineage and licence.
