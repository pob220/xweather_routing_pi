# xWeatherRouting 1.26.0 alpha — land clearance and chart performance

This release reduces redundant work in chart-aware passage planning. The plugin
services the shared host request queue once per computation timer update, rather
than once for each waiting route. Its existing 16-request / 50 ms service budget
is shared by concurrent routes; a single synchronous host chart operation can
still exceed that time budget.

The companion OpenCPN patch prepares eligible CM93 area rules once for each
safety tile, decodes their unchanged land/drying/depth attributes once, and
retains exact polygon selection at every existing grid point. Missing CM93 cells
no longer rebuild geometry or depth-contour tables when nothing was loaded.
Semantic extraction suppresses repeated global busy-cursor changes inside a
nested thread-local scope; normal rendering keeps its busy-cursor behaviour.
Borrowed rule references are discarded before any fallback query that can change
the prepared chart working set.

## Compatibility and safety

The plugin still uses API 1.21 and discovers chart-safety services dynamically.
On stock OpenCPN, chart-awareness controls remain disabled; ordinary weather
routing and GSHHG land avoidance continue to work. A stock OpenCPN weather route
was completed during qualification. Desktop and Android retain their existing
interfaces. Upgrading from 1.24 or 1.25 preserves customised boats and polars
without an import/replacement prompt.

The measured chart-preparation gains require the companion OpenCPN changes.
Installing only the plugin on an older enhanced host provides the shared queue
budget, but does not replace that host's chart-classification implementation.
Stock OpenCPN does not acquire chart-awareness merely by installing this plugin.

Grid resolution, chart priority, minimum charted depth and missing-depth
handling are retained. Land-clearance enforcement is corrected: a binary
inside/clear adapter result could keep departure access active while following
the coast, and final checks trusted the resulting leg flag. Search, independent
chronological replay and delivered plotting chords now enforce the same local
endpoint bound: 1.5 times the clearance, with a 0.5 NM minimum and 2 NM maximum.
Outside that local access area the full configured clearance is required. An
endpoint flag cannot waive an arbitrarily long chord; actual land, exclusions
and depth remain checked inside the local access area. An offshore departure
is checked normally rather than forcibly marked as inside the buffer.

CM93 preparation is optimised, with existing o-charts behaviour retained. It does
not introduce the separately planned S-57/S-63 providers or change xGRIB's
handling of the date line.

## Qualification

The earlier performance qualification, before the clearance correction, uses
fixed NOAA GFS data for the Hawaiian test area, a
Lagoon 560 cruising-catamaran polar, Quinton's saved engine parameters, full
GSHHG, 1.5 m minimum depth and 0.1 NM clearance. Chart awareness remains enforced.
The benchmark is a substitute workload, not a reproduction of Quinton's missing
original forecast and chart configuration, and its endpoints are test inputs.

Sequential unprofiled timings on the local laptop were:

| Calculation | 1.25 / existing core | 1.26 / companion core |
| --- | ---: | ---: |
| Cold passage | 356.692 s | 130.825 s |
| Warm passage | 21.326 s | 21.353 s |
| Cold preparation | 59.447 s | 22.422 s |

The cold passage improved 2.73×; the warm passage showed no measured improvement.
The four runs retain the same arrival, passage duration and routed distance,
and pass final plotted-route safety validation. A comparison of 7,083,734 cells
across 4,214 common tiles found identical class, hazard flags, depth and depth
completeness. Additional tiles reflect changed prefetch timing.

The local report records the individual runs and cell comparison.
It also records 402 plugin unit tests, 116 companion-core tests, six publication
contract tests, 19 real-chart checks, eight unchanged routing-result comparisons,
stock-host routing, concurrent chart-aware departures, and the unchanged native
route workflow when the checker is skipped. Android arm64 is compiled against
the stock OpenCPN core library; packages include manual-import metadata and
verified bundled shoreline data. Physical Android and Windows execution are
not newly qualified by this local pass; the existing platform CI remains
required before publication.

The earlier performance binaries and import packages were prepared locally.
Those packages require rebuilding after the clearance correction before alpha
publication. The corrected native build is qualified separately below.
Provider expansion remains separate.

## Clearance regression qualification

The clearance fix includes a reproduction of a synthetic 12 NM route which
remains entirely inside a 1 NM buffer and previously passed independent replay.
Both that route and a forged long departure chord must now fail, while local
coastal departure followed by an offshore passage remains valid. Land and depth
checks remain active during local access. The previous benchmark timings above
are not new measurements of the clearance-corrected build.

The complete standalone suite passes all 405 tests, with the five subsequently
added solver/arrival regressions also passing (410 distinct tests). The solver checks
exercise Quick, Standard, Alternative and Professional against a binary
inside/clear provider. The standalone pre-fix reproduction now fails after
6 zero-margin checks and its first full-margin check; previously it accepted
all 12 NM using 48 zero-margin checks and no full-margin checks. A 2 m sample
is rejected with the 3 m constraint, while a 3 m sample passes.

An isolated test using the working OpenCPN host, installed o-charts provider,
Full GSHHG and the current combined GRIB completes the offshore north-Anglesey
control at 1 NM clearance and 3 m minimum depth (28.147 NM, 4 h 39 min 51 s).
Its chronological and final plotted-route validation pass; the private test
leaves the working profile and plugin unchanged. This is a control passage,
not the original coastal route.

The Holyhead local-departure control also completes at 1 NM / 3 m (4.953 NM,
52 min 20 s), retaining ten-minute initial legs and returning to full clearance
offshore. The original Holyhead–Conwy 19 October 12:00 UTC test does not find a
valid route with the selected search settings. A separate 180-degree course
angle test exhausts its graph state allowance. Neither failure is evidence
that no safe passage exists; these runs must not be reported as successful
coastal passage qualification. Their failed results are retained with the
control results and regression logs.

The separate offshore-to-Conwy arrival control also exhausts the graph state
allowance. Independent engine coastal-arrival regressions pass, but a successful
real-chart arrival into this Conwy waypoint is not claimed by this pass.
