# xWeatherRouting

The optional hardened-OpenCPN planning-provider boundary is documented in
[docs/external_control_provider_preview_b.md](docs/external_control_provider_preview_b.md).
Stock OpenCPN remains supported through the unchanged plug-in API 1.21.

xWeatherRouting is an experimental, standalone OpenCPN weather-routing
plugin derived from the established Weather Routing plugin. It retains the
existing configuration, polar and GRIB integrations while adding the modern
deterministic routing engine developed on this branch.

The plugin currently provides:

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
not expose the optional chart-backed service, so xWeatherRouting disables
those two controls and continues to use the standard plugin GSHHS checks.
There is no direct enhanced-core symbol dependency.

Do not enable the standard Weather Routing plugin and xWeatherRouting at the
same time. xWeatherRouting deliberately retains the established
`/PlugIns/WeatherRouting` settings and user-data layout so existing routes,
boats, polars and preferences migrate without conversion.

## Building

For a clean standalone build against the vendored stock OpenCPN 1.21 API:

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DXWEATHER_ROUTING_STANDALONE_API=ON \
  -DOCPN_BUILD_TEST=OFF
cmake --build build-release --parallel
cmake --build build-release --target package
```

Keep release packaging in a build directory where tests are disabled. This
prevents test-only GoogleTest libraries from being included by older
packaging infrastructure.

## Testing

```sh
cmake -S . -B build-test -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DXWEATHER_ROUTING_STANDALONE_API=ON \
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

## Status

The package name, library, catalogue and UI identity are distinct from the
standard plugin: `xweather_routing_pi` / xWeatherRouting. Cross-platform
artifacts may be built for validation, but publishing to an OpenCPN catalogue
is a separate release decision.

## Licence and acknowledgement

The plugin is GPL v3 or later. The original Weather Routing plugin was written
by Sean D'Epagnier and has benefited from many OpenCPN contributors,
translators and testers. xWeatherRouting preserves that lineage and licence.
