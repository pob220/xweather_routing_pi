# What the per-route results tell us

The matrix now covers **115 case entries and 908 recorded calculations** from
three separate campaigns. No engines were changed or rerun to produce it.

- [Full timing matrix and grouped tables](MATRIX.md): readable on GitHub and
  suitable for copying individual Markdown tables into Zulip.
- [Interactive matrix](matrix.html): download the file and open it in a browser;
  filter by campaign, route setting, direct distance, forecast or case name.
  Click a cell for its outcome, stopping reason and passage time. It works offline.
- Colour matrices: [mixed suite](synthetic-matrix.png),
  [archived suite](headless-matrix.png), [Tonga](tonga-matrix.png).
- [Measurements](measurements.json) and [grouped statistics](statistics.json):
  sanitized data, without private forecasts, charts, polar files or local paths.

## Fast at what, and successful how often?

These are single cold observations, not statistically established rankings.
Expected rejection cases are separate. Intended positive cases are attempts to
find routes, not proofs that every supplied forecast makes a passage feasible.

The archived suite has 59 positive cases and six rejection cases. Its positive
results are:

| Engine | Valid / 59 | Median successful calculation | Mean successful calculation |
| --- | ---: | ---: | ---: |
| Quick2 | 50 | 0.322 s | 0.463 s |
| MIM+coastal A* | 27 | 0.353 s | 0.610 s |
| Atlas | 47 | 1.392 s | 1.417 s |
| First MIM+A* | 45 | 1.361 s | 1.731 s |
| Quick | 50 | 3.089 s | 3.081 s |
| Standard | 51 | 0.922 s | 1.639 s |
| Alternative | 50 | 1.448 s | 1.782 s |
| Professional | 31 | 6.164 s | 7.216 s |

Success-only columns can cover different cases. The full report also supplies
failure medians/means and comparisons restricted to the **same 19 cases all
eight engines solved**. It separates synthetic weather from stored GRIB data
and short, regional and long endpoint distances.

The coastal/harbour subset illustrates why Quinton's request matters: the second
prototype's successful median is only 0.119 s, but it solves **11/32**; Quick2's
median is 0.407 s while solving **29/32**. The smaller number alone would give
the wrong impression. Conversely, the six ocean/offshore cases favour other
methods for coverage: Quick solves five and Atlas four; Quick2 and the coastal
prototype each solve one. Six cases are too few for a general ocean-routing
claim.

Across the six existing engines, at least one completed 57/59 archived positive
cases. Quick2 and Standard together cover 54/59. These are unions of standalone
results, **not measured Auto success rates or latencies**. They show why a set of
complementary engines can be useful. There is still no exclusive success for
the second prototype in these measurements.

The suite contains many Holyhead departure variants. Grouped statistics help,
but this is still a development suite rather than a representative sample of
worldwide sailing. Future ranking changes should be checked with repeated
runs, cold/warm caches and held-out route families, rather than tuning to this
matrix alone.

## Auto and All remain useful

The user-facing distinction is sensible: **Auto for a responsive validated
result; All for a broader comparison of alternatives**. Improving the engines
and shared data access helps both modes.

The current implementation has two details worth describing accurately:

1. Auto follows the enabled engine sequence. A Quick2 completion is retained
   while selected later engines can still run; it does not always return the
   first completed Quick2 route. It has no universal wall-time guarantee.
2. All runs every engine selected for All in Advanced and retains accepted
   alternatives. Atlas is optional in the shipped default membership. All is
   broader, but each search remains bounded; it is not a mathematical exhaustive
   search of all possible voyages.

Source: [v1.34.1 engine sequence](https://github.com/pob220/xweather_routing_pi/blob/v1.34.1-alpha.1/include/engine/native/RoutingEngineSequence.h#L80),
[membership defaults](https://github.com/pob220/xweather_routing_pi/blob/v1.34.1-alpha.1/include/RoutingEngineSettings.h#L21).
These files match the released tag in the checked local source. This report
does not change their behaviour or add either prototype to the plugin.

The matrix gives a basis for testing a better engine order or budget policy.
An offline union or sum of separate runs cannot establish the actual behaviour
of Auto, whose engine settings, caches and stopping policy also matter.

## Quinton's chart-aware refinement proposal

It is a sensible next experiment: use inexpensive geography to find several
different approaches, then spend chart-checking effort where it can change the
decision. The expensive work includes chart extraction as well as route search.

There are already related mechanisms:

- The plugin's GSHHG scout guides chart-data preparation. It is a geometry hint
  and is never delivered as the authoritative completed route. See
  [CollectChartSafetyScoutGeometry](https://github.com/pob220/xweather_routing_pi/blob/v1.34.1-alpha.1/src/WeatherRouting.cpp#L10779).
- Atlas accepts a cheaper geography provider for search guidance while keeping
  the supplied original provider for physical edges and final acceptance. See
  [Atlas options](https://github.com/pob220/xweather_routing_pi/blob/experimental/mim-astar-coastal/vendor/atlas-routing-engine/include/atlas_routing/Engine.h).
- The core proposal supplies immutable derived chart data to workers, with
  mutable chart extraction serviced on the GUI thread. See
  [OpenCPN PR #5475](https://github.com/OpenCPN/OpenCPN/pull/5475).

What is not yet implemented by this standalone experiment is the proposed
general process of repairing selected chart-blocked sections of the top few
GSHHG routes. A controlled implementation should:

1. Retain candidates taking different sides of islands or different channels,
   rather than several nearly identical tracks. Bound the candidate count and
   chart-preparation area explicitly.
2. Inspect relevant chart coverage and hazards along the complete candidate,
   then refine around detected hazards. Distance from GSHHG land alone cannot
   identify offshore reefs, rocks or shoals. A GSHHG-blocked channel can also
   require a chart-guided alternative before the first candidate exists.
3. Repair a blocked section using the boat's actual arrival time and sailing
   state. A detour changes the weather encountered later: recalculate the
   remaining voyage and replay the complete result before accepting it.
4. Widen a repair, reopen an earlier choice or try another whole-route approach
   when a local fix fails. A fixed small repair corridor can miss the useful
   passage entirely. Missing chart authority must not become permission to sail.

This would complement Auto/All. First test it on a small chart-aware headless
set with known hazards and identical physical inputs, measuring success,
arrival time, chart work and total latency. It should not be inferred to work
from the GSHHG-only timing matrices here.

## Is OpenCPN the performance bottleneck?

It can add latency, but the current standalone comparisons cannot quantify
that overhead: their chart authority and weather adapters differ from the
plugin. Replacing the frontend would still require equivalent data and hazard
handling.

There is a useful historical example in the saved, fixed-date-line chart-aware
Tonga test. Its harness recorded 8.607 s from route-test start to completion;
the modern-native solver summary recorded 3.775 s. The start-route setup interval
was 4.788 s. Final display validation logged under one millisecond and the
completion timer tick one millisecond. Chart requests were also serviced during
the solve.

Those intervals overlap or include other work. The 4.832 s outside the solver
summary must not all be called GUI overhead: it includes preparation and
scheduling, and the solver interval itself can include chart-service waits.
Display validation is not a measurement of GPU painting or every UI callback.
This was a historical v1.34/date-line test, not a new v1.34.1 benchmark or a
feature-matched comparison with another application.

The next performance measurement should separate input preparation, GSHHG
scouting, chart extraction/cache work, search, validation and result-to-display
latency, using the same route, forecast, polar, constraints and engine settings.
Time chart work separately but also include it in the end-to-end total. Test
both cold and warm caches. Existing timing logs provide a useful starting point.

Architecturally, keep one standalone engine and provider interface that OpenCPN
or a lighter frontend can call. Prepare reusable immutable chart/weather data,
batch expensive host requests and limit preview drawing. Decide on a different
frontend after measuring this boundary. Moving the same calculation elsewhere
will not fix the coastal prototype's internal stored-history exhaustion.

## Rebuild this presentation

With Python and Matplotlib installed, the public sanitized data can recreate
the tables, offline interactive report and PNG matrices without any private
forecasts or routing calculations:

```sh
python3 experiments/mim-astar-coastal/tools/build_breakdown.py \
  --dataset experiments/mim-astar-coastal/breakdown/measurements.json \
  --output my-breakdown
```

Use `--no-plots` to omit Matplotlib. The source-import path additionally validates
the recorded input and adapter provenance; see the script's `--help`.
