# Standalone MIM and A* routing experiment

This C++20 prototype is for headless testing and modification. It has no
demonstrated advantage over the existing engine portfolio and no current
production use case. It is not registered with xWeatherRouting or connected to
the plugin build. Only this experimental folder is added on this branch.

[Build and headless testing instructions](QUINTON-TESTING.md) give a complete
clone/build/smoke-test sequence and an example for your own stored forecast.
[Comparison summary](QUINTON-SUMMARY.md) separates the original and expanded
campaigns. [C++ review](CODE-REVIEW.md) explains the guideline checks and human
readability changes. `review-verification.json` records the checks on this
published source revision.

## Source and dependencies

The branch starts from public xWeatherRouting commit
[`7156844`](https://github.com/pob220/xweather_routing_pi/commit/7156844dec9ab1bccb826fa787ac426af17b5fbd).
`baseline.json` fingerprints the shared core. The experiment's own CMake project
reuses the repository's four existing vendor trees for physical integration,
validation, the six comparison engines and benchmark fixtures. None is changed
by this experiment. No OpenCPN application, GUI libraries or display is required.

The library needs C++20 and CMake 3.20+. Its comparison client also needs Python
3.8+, pkg-config and JsonCpp; ecCodes enables real-GRIB tests. The testing guide
uses Ninja. Forecasts, chart data, user profiles and Boat.xml files are not
bundled. The supplied synthetic examples require no external weather data.

## Search and limitations

MIM advances modified isochrone fronts and retains a candidate with the best
estimated arrival plus an outward alternative for each sector/physical family.
A* then starts again at departure. It keeps the validated MIM route as an
incumbent but does not inherit a corridor that might exclude a useful detour.
Separate MIM-only and A*-only modes support controlled comparisons.

The default A* partition bins space and headings and caps alternatives. Every
node retains exact UTC and physical/resource history, but the quota is lossy;
it is not exact Pareto dominance. `exactStateKeys` removes that quota for
distinct A* continuation states, while MIM, sampled headings, finite work and
terminal numerics remain approximate. No global optimality or completeness is
claimed.

`guidanceSpeedKnots` is only a search hint. A nonzero
`certifiedMaximumGroundSpeedKnots` is a caller guarantee for every permitted
performance/current/propulsion state. With no certificate, A* uses a zero
heuristic. The benchmark can derive a certificate from its controlled polar
tables and current envelopes; `prepare_case.py` prepares the latter for stored
GRIBs. This reasoning must not be assumed for arbitrary host providers.

Motion and waiting use the existing checked physical kernel. Every accepted
incumbent is replayed by the chronological validator, and the comparison client
runs a fresh external replay. Ordinary resource exhaustion can preserve a
validated incumbent. Cancellation, a violated speed certificate or an
authoritative validation escalation rejects delivery.

## API and code layout

`include/mim_astar/Router.h` documents units, option ranges, resource accounting,
callback lifetime and delivery rules. `src/Router.cpp` contains the request-local
search controller. `src/SearchStorage.h` contains named continuation keys,
parent-linked nodes, deterministic queue ordering and the PMR allocation budget.
`tests/` holds contract checks and explicit offline fixture adapters. `tools/`
holds the client generator, serial comparison, case preparation and review tools.

```cpp
#include "mim_astar/Router.h"

mim_astar::Options options;
// Keep zero unless a bound for ALL permitted ground velocities is justified.
options.certifiedMaximumGroundSpeedKnots = 0;
auto result = mim_astar::Router{}.route(request, environment, options);
```

The router owns no providers and creates no threads. Callers supply water-relative
wind and providers suitable for their concurrency model. Supported objectives
are deterministic minimum time, within ±85° latitude. The prototype does not
prepare current-prediction fallback coverage. Its PMR cap covers search containers,
not all process/provider/string/output allocations.

For the library alone, configure this folder with `-DMIM_BUILD_TESTS=OFF` and
link the CMake target `mim_astar`; JsonCpp, Python and ecCodes are then unnecessary.
Run `python3 experiments/mim-astar/tools/check_code.py --build build-mim-astar`
from the repository root, as shown in the review guide. clang-format and
clang-tidy are optional review dependencies. Sanitizer builds use
`-DMIM_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`.

The code is GPL-3.0-or-later, consistent with the repository license and file
SPDX headers. The search is independently expressed; reuse of shared physics,
fixtures and validation is deliberate, not a formal source-isolation clean-room
claim. Mathematical background includes
[Chen's thesis, section 3.2.3](https://research.chalmers.se/publication/542501/file/542501_Fulltext.pdf)
and [time-dependent shortest paths](https://people.csail.mit.edu/bdean/tdsp.pdf).

Real-CM93 follow-up: [short results](TONGA-CM93-SUMMARY.md),
[detailed comparisons](TONGA-TEST-DETAILS.md), and
[installed v1.34 date-line assessment](V134-DATELINE-ASSESSMENT.md).
