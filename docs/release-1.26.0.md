# xWeatherRouting 1.26.0 alpha — chart-awareness performance

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

Grid resolution, chart priority, clearance, minimum charted depth, missing-depth
handling and final plotted-route validation are retained. This is a focused
CM93 performance release, with existing o-charts behaviour retained. It does
not introduce the separately planned S-57/S-63 providers or change xGRIB's
handling of the date line.

## Qualification

Local qualification uses fixed NOAA GFS data for the Hawaiian test area, a
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

The binaries and import packages are prepared locally. Installation, alpha
publication and provider expansion are separate actions.
