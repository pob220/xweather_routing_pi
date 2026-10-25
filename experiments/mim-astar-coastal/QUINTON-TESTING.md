# Test the second engine headlessly

This is a separate standalone comparison client. It does not install or
modify OpenCPN or its plugins. The existing OpenCPN headless kit still tests
the installed engines; use this client for the new experiment.

On Linux, use a C++20 compiler, CMake 3.20+, Ninja, pkg-config, JsonCpp and
Python 3.8+. Real-GRIB runs additionally need ecCodes development files and
`grib_get`. No GUI, display or OpenCPN profile is needed.

The sparse clone below avoids downloading the plugin and bundled shoreline
data when you already have a usable shoreline file:

```sh
git clone --filter=blob:none --no-checkout --single-branch \
  --branch experimental/mim-astar-coastal \
  https://github.com/pob220/xweather_routing_pi.git mim-coastal-test
cd mim-coastal-test
git sparse-checkout init --cone
git sparse-checkout set experiments/mim-astar experiments/mim-astar-coastal \
  vendor/weather-routing-engine vendor/original-routing-engine \
  vendor/quick2-routing-engine vendor/atlas-routing-engine research/sixth
git checkout

cmake -S experiments/mim-astar-coastal -B build-mim-coastal -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-mim-coastal --target coastal_benchmark coastal_contracts --parallel 4
ctest --test-dir build-mim-coastal -R '^coastal-' --output-on-failure
python3 experiments/mim-astar-coastal/tools/check_baseline.py
```

Run the synthetic island case without downloading weather or shoreline data:

```sh
./build-mim-coastal/coastal_benchmark mim_coastal \
  experiments/mim-astar-coastal/examples/island-handoff.json 10 1
```

Only `"valid": true` counts as a completed result. Each completion also gets
a fresh external replay in this client. `coastal_diagnostics` records ocean
and coastal work, resumed alternatives, hint queries, map-building cost and
the reason the search stopped.

Compare the first engine, second engine and six fixed existing controls:

```sh
python3 experiments/mim-astar-coastal/tools/run_comparison.py \
  --binary build-mim-coastal/coastal_benchmark --output comparison-01 \
  --generated 8 --timeout 10 --repeats 2 \
  --engines mim_astar mim_coastal quick2 atlas original quick alternative main
python3 experiments/mim-astar-coastal/tools/analyze.py comparison-01
```

Use a new output directory for each run. `original` is Quick, `quick` is
Standard and `main` is Professional. `mim` and `astar` select the first
experiment's separate stages. CPU pinning is optional (`--cpu N` must name an
allowed CPU). The common protocol runs serially and retains binary/input hashes.

## Your stored forecast and installed shoreline

Inspect available OpenCPN shoreline files:

```sh
python3 experiments/mim-astar-coastal/tools/shoreline_files.py
```

Copy `examples/offline-grib.template.json` to `my-route.json`. Edit the start,
finish, UTC Unix departure epoch, GRIB and single polar-table paths. Set
`"shoreline": "auto"` to select the finest available known-layout file, or
supply an explicit path. For a custom layout, use the discovery script's
`--root /your/data/directory` and copy the chosen path. An optional separate
`"guidance_shoreline": "auto"` explicitly selects a coarser hint file; the
authoritative `shoreline` remains unchanged.

```sh
python3 experiments/mim-astar-coastal/tools/prepare_case.py my-route.json \
  --output prepared-route.json
./build-mim-coastal/coastal_benchmark mim_coastal prepared-route.json 20 1
```

Preparation resolves and hashes the files, expands compressed shoreline files
beside the prepared case without changing the originals, normalises polar delimiters and
derives a current-speed envelope for the supported offline decoder. It writes
`prepared-route.suite.json` for `run_comparison.py --extra-cases`. Keep the
departure inside forecast coverage; do not waive missing data or safety
constraints simply to obtain a completion.

The standalone decoder supports regular-grid wind and GRIB1 current
parameters 49/50. It does not decode waves, Boat.xml's multiple profiles,
climatology or CM93/chart-depth authority. App-kit nested scenarios need their
original providers; this preparation tool rejects them rather than pretending
to reproduce them.

Useful controlled variants are `coastal_astar` (coastal search everywhere),
`mim_coastal_no_hints`, `mim_coastal_stall_recovery` (deferred work only after
active work stalls) and `mim_coastal_improve` (continue after the first result).
The last mode can use the full allowance; it should be compared at a stated
deadline. JSON controls include `coastal_band_nm`, `coastal_step_seconds`,
`coastal_fan_batch`, `coastal_heading_step`, `coastal_ocean_heading_step`,
`coastal_grid_cells`, `coastal_motions`, `coastal_labels` and
`coastal_guidance_weight`. Review defaults and limitations in the README.

For library-only builds, use `-DCOASTAL_BUILD_TESTS=OFF` and link `mim_coastal`;
the comparison client's JsonCpp, Python and ecCodes requirements then disappear.
The library source is GUI-independent; this comparison harness's documented
and tested platform is Linux, not a claim of qualified Windows/Android packages.
