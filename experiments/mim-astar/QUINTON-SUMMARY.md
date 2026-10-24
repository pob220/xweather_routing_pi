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

The prototype is a useful test of the idea, but the measurements do not yet
justify replacing the existing engines. The next experiment should target the
cost of physical integrations and the pruning/work-allocation decisions on the
retained failure cases.
