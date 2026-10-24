# Testing the standalone MIM+A* experiment

This is an experimental C++20 library and command-line comparison client. It
has no plugin selector and is not connected to the current xWeatherRouting
plugin build. The earlier `run_headless.py` kit launches OpenCPN and continues
to test the installed engines; use the standalone commands below for MIM+A*.

On Linux, install a C++20 compiler, CMake 3.20+, Ninja, pkg-config, JsonCpp and
Python 3.8+. Install ecCodes development files and its `grib_get` command for
real-GRIB tests. clang-format/clang-tidy are optional review tools. No display,
OpenCPN installation or test profile is needed for this client.

```sh
git clone --depth 1 --single-branch --branch experimental/mim-astar-standalone \
  https://github.com/pob220/xweather_routing_pi.git mim-astar-test
cd mim-astar-test
cmake -S experiments/mim-astar -B build-mim-astar -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mim-astar --parallel 4
ctest --test-dir build-mim-astar --output-on-failure

python3 experiments/mim-astar/tools/run_comparison.py \
  --binary build-mim-astar/mim_benchmark --output mim-smoke-results \
  --generated 0 --case-filter reach --repeats 2 --timeout 20 \
  --engines mim_astar quick2 atlas original quick alternative main
python3 experiments/mim-astar/tools/analyze_headless.py mim-smoke-results
```

The smoke test needs no downloaded forecast or shoreline. `original` means
current **Quick**, `quick` means **Standard**, and `main` means **Professional**.
All seven use the same physical model and independent replay. Count only
`valid: true` as success. Results include `results.jsonl`, `comparison.md`,
`route-matrix.csv` and `exclusive-analysis.json`. Use a new output directory
for each run. Remove `--case-filter reach` and set `--generated 32` for a broader
analytic suite; keep its results separate from the smoke test.

For your own offline route, copy `experiments/mim-astar/examples/offline-grib.template.json`
to `my-route.json`. Replace the endpoints, UTC Unix departure epoch, GRIB path,
single polar-table path and uncompressed GSHHG path with your inputs; review
all constraints. The departure must fall inside the forecast. Then run:

```sh
python3 experiments/mim-astar/tools/prepare_case.py my-route.json --output prepared-route.json
./build-mim-astar/mim_benchmark mim_astar prepared-route.json 20 1 > my-route-result.jsonl
```

Preparation normalizes polar delimiters without changing numeric values and
derives a current-speed certificate from all supported GRIB current frames.
It also writes `prepared-route.suite.json` for `run_comparison.py --extra-cases`
and a receipt with input hashes. To compare all seven on that case, use the
comparison command above with `--extra-cases prepared-route.suite.json`,
`--case-filter my_offline_route` and a new output directory.

This standalone schema differs from the app kit's nested JSON. Boat.xml,
chart/depth authority, climatology and departure optimization require their
original app providers; they are not silently converted. The offline reader
decodes regular-grid wind and GRIB1 current parameters 49/50, and does not
decode waves. Keep required-data failures as failures rather than relaxing a
saved route's constraints to obtain a completion.

Edit `experiments/mim-astar/src/Router.cpp`, its public header and private
storage types in your clone, rebuild, and rerun into a fresh results directory.
The documented interface, approximation rules and review checks are in the
folder's README and `CODE-REVIEW.md`. The measured prototype has no demonstrated
portfolio advantage; this branch exists for reproducible experiments.
