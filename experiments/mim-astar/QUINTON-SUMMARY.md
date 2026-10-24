**MIM+A* prototype results**

A standalone C++20 MIM+A* prototype is implemented and tested. It has a documented
API, named domain types, RAII and request-local search state. It shares the
existing physical integration and independent route validator with the other
engines; the search implementation is independently expressed.

**Original comparison: 74 intended-positive cases, 10-second solver deadline**

These are the first/cold trial figures from the original campaign:

| Engine | Valid routes / 74 | Median calculation |
|---|---:|---:|
| MIM+A* | 68 | 0.82 s |
| Quick2 | 69 | 0.09 s |
| Atlas | 72 | 0.56 s |
| Professional | 55 | 2.44 s |

A* improved the arrival time on 25 cases completed by both plain MIM and the
hybrid. However, splitting the work allowance between the two stages lost some
MIM-only completions. The hybrid improved the best existing-engine arrival on
six cases by 5–177 seconds, but supplied no exclusive completion.

**Expanded existing headless suites: 59 intended-positive cases, 20-second solver deadline**

We then ran 65 additional scenarios, including 34 stored-GRIB cases and six
deliberate negatives: 455 trials across the prototype and all six current engines.

| Engine | Valid routes / 59 | Median calculation |
|---|---:|---:|
| MIM+A* | 45 | 1.31 s |
| Quick2 | 50 | 0.32 s |
| Atlas | 47 | 1.87 s |
| Quick | 50 | 3.61 s |
| Standard | 51 | 0.88 s |
| Alternative | 50 | 1.47 s |
| Professional | 31 | 14.61 s |

**No exclusive MIM+A* route was found in either campaign.** In the expanded
suite, Quick2 completed every route completed by MIM+A*, plus five more.
MIM+A* also produced no arrival more than one second earlier than the best
validated existing-engine arrival in that expanded suite.

MIM+A* completed all nine Holyhead–Conwy departures and all three departures
for each of Calf of Man, Douglas and Peel. It missed all three Lough Foyle
departures, which Quick2, Standard and Alternative completed. Eleven of its
14 positive-case failures reached a work allowance before the wall-clock
deadline, so allowing more seconds alone would not resolve them.

Both tables include failed solves in their calculation medians and exclude
forecast preparation and the fresh external replay. The campaigns have different
inputs and deadlines and should be assessed separately. Every counted completion
passed independent chronological validation. The additional real-weather cases
use offline GRIB and GSHHG; full chart/depth, Boat.xml and climatology scenarios
require separate app integration tests.

The expanded campaign had no process errors and rejected all 42 negative trials.
Verification passes 22 Release test groups and 11 ASan/UBSan groups; leak detection
is disabled under the existing sandbox tracing restriction. Existing engine
libraries and installed plugins are unchanged. Cowboy remains a separate
historical control because its older physical/provider model makes its raw
timings unsuitable for this common-model ranking.

**Cowboy: main problems found in the examined legacy implementation**

- **Premature completion:** enclosing the destination inside an isochrone could
  trigger “reached” even when a feasible final sailing connection had not been made.
- **Unsailable plotted final legs:** implicit optimized tacking could be drawn
  as one straight chord, without explicit sailed tacks. The displayed final leg
  could violate the configured wind-angle limits or imply excessive straight-leg speed.
- **Land and clearance gaps:** long final connectors could skip land checks and
  omit the configured margin. Inherited host intersection/grid-traversal defects
  could also miss crossings.
- **No mandatory independent whole-route acceptance replay:** a displayed route
  was not required to pass a fresh chronological check of the complete delivered
  track against sailing physics and the requested constraints.
- **Direction-dependent final-connection bugs:** the iteration did not update its
  course estimate correctly and used a one-sided angular-error test. A route could
  fail in one direction while the reversed control succeeded.
- **Permissive missing-data handling:** unavailable current could become assumed
  zero current, and missing swell could bypass an active wave limit.

These are implementation and acceptance problems addressed by the hardened Quick
engine; the contour method itself remains useful. Some Cowboy routes did pass
independent replay. The problems do not imply every historical route was invalid.
[Legacy arrival code](https://github.com/rgleason/weather_routing_pi/blob/ed07e567c22e5c65d1711f7a4ded654101912043/src/RouteMap.cpp#L401),
[legacy final connector](https://github.com/rgleason/weather_routing_pi/blob/ed07e567c22e5c65d1711f7a4ded654101912043/src/RoutePoint.cpp#L539),
[Quick hardening](https://github.com/pob220/xweather_routing_pi/blob/7156844dec9ab1bccb826fa787ac426af17b5fbd/vendor/original-routing-engine/README.md).

The prototype is a useful test of the idea, but the measurements do not yet
justify replacing the existing engines. The next experiment should target the
cost of physical integrations and the pruning/work-allocation decisions on the
retained failure cases.
