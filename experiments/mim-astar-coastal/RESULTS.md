# Second MIM + coastal A*: initial results

25 October 2026. This prototype works headlessly, but **does not improve overall
route coverage over the first prototype**. It is an experiment for reading,
replaying and modifying, not a proposed replacement for an installed engine.
The first experiment and six frozen comparison engines remain unchanged.

## What was tested

All calculations used the same Linux laptop, Release build and serial comparison
protocol, with solver processes pinned to logical CPU 4. A completion counts only
when both the engine's whole-route check and the client's additional chronological
replay pass against the supplied weather, polar and geography providers.

These are preliminary, single cold trials. Failed calculations remain in the
median; a smaller median alongside fewer successes does not establish a faster
equivalent solver. Times include engine-side geographic guidance and internal
validation. Forecast preparation and the client's extra replay are outside the
calculation timer. Deadlines are requested solver deadlines, not guaranteed hard
process-wall limits; synchronous provider calls can return later.

The control measurements were retained from the immediately preceding runs.
Only the second engine was rerun after its search-bookkeeping fixes. A local
combination check verified matching scenario settings, content hashes of input
files and shared source hashes. Binary hashes and compact machine-readable
summaries are in [verification.json](verification.json). The final client also
serializes an unset closest-distance diagnostic as JSON null; its compiled
second-engine object is byte-identical to the object used for the timed runs.

## Mixed synthetic and shoreline cases

39 positive cases plus eight deliberate rejection cases; requested deadline
five seconds. This is the development suite, not an untouched holdout. It includes
analytic sailing cases, changing weather/current, islands, narrow passages,
date-line crossings and real-shoreline fixtures, rather than stored-GRIB voyages.

| Engine | Valid routes / 39 | Median calculation |
| --- | ---: | ---: |
| First experiment: MIM only | 35 | 0.641 s |
| First MIM+A* | 36 | 1.537 s |
| Second MIM+coastal A* | 33 | 0.340 s |
| Quick2 | 35 | 0.176 s |
| Atlas | 36 | 0.866 s |
| Quick | 28 | 0.792 s |
| Standard | 35 | 0.672 s |
| Alternative | 35 | 1.201 s |
| Professional | 20 | 4.915 s |

All eight deliberate rejection cases were rejected by every engine; there were
no process errors. The second engine found no route which the first missed in
this suite, and no route missed by all six existing controls. Of their 33 shared
completions, the second arrived over one second earlier in seven and later in
20. Calculation time and voyage arrival time are different measures.

## Larger archived headless suite

59 positive cases plus six deliberate rejection cases; requested deadline
20 seconds. Of the positive cases, 34 use stored real GRIB data. These are the
same supported standalone scenarios replayed by both prototypes, not a claim
that the offline decoder reproduces every feature of the OpenCPN app kit.

| Engine | Valid routes / 59 | Valid stored-GRIB routes / 34 | Median calculation |
| --- | ---: | ---: | ---: |
| First MIM+A* | 45 | 23 | 1.401 s |
| Second MIM+coastal A* | 27 | 6 | 1.065 s |

Both rejected all six negative cases; no process errors occurred. The second
engine solved two cases the first missed, but missed 20 the first solved.
Of 25 shared completions, it arrived over one second earlier in 11 and later
in eight. The remaining six were within one second.

We then replayed **all six existing controls** on both second-only cases under
the same 20-second requested deadline:

| Case | Second prototype | Existing controls that also completed |
| --- | --- | --- |
| `headless_lab_holyhead_conwy_f` | Valid | Quick; Standard |
| `headless_real_cape-hout` | Valid | Quick; Professional |

Consequently, these results establish no route that only the second engine can
solve. The full six-engine portfolio was tested on the smaller suite and these
two additional cases, rather than rerun across all 59 archived cases.

Of the second engine's 32 positive-case failures, 17 reached the stored-history
limit, 13 reached a motion/generated-state/weather limit, one failed preflight
because of missing coverage, and one exhausted retained search. This is evidence
that retaining many alternatives consumes the budget before enough useful
progress is made; it is not evidence that those passages are impossible.

## Tonga passages with real GFS and full GSHHG shoreline

Requested deadline 20 seconds. A cell gives completion and calculation time;
failure times are retained so a quick failure is not mistaken for a fast solve.

| Passage | First MIM+A* | Second MIM+coastal A* | Quick2 | Atlas |
| --- | --- | --- | --- | --- |
| Saved Pukutalo → Vuda | Failed, 2.342 s | Failed, 1.257 s | Valid, 2.402 s | Valid, 3.691 s |
| Representative Nuku'alofa → Vuda | Failed, 2.793 s | Failed, 1.277 s | Valid, 3.078 s | Valid, 2.437 s |
| Screenshot-derived Lalona → Uoleva | Valid, 0.130 s | Valid, 0.251 s | Valid, 0.088 s | Valid, 2.401 s |

The second engine's two failures reached the stored-history limit. Its valid
Lalona–Uoleva passage arrived 2,148 seconds earlier than the first prototype's
result, but took longer to calculate. The screenshot-derived endpoints are a
representative reconstruction, not Quinton's exact saved routing configuration.

These tests use shoreline avoidance only. **They do not qualify CM93 depths,
reef clearance or the installed OpenCPN chart-aware API.** No charts were changed
and no test OpenCPN instance was needed for these headless runs.

## Checks and interpretation

- Eight new regression groups pass in Release and with AddressSanitizer plus
  UndefinedBehaviorSanitizer. LeakSanitizer was disabled for the sandboxed run.
- Checks exercise constant-speed arrival, date-line handling, island detours,
  an ocean/coastal handoff, deferred alternatives, explicit timed waiting,
  a deliberately wrong shoreline hint, hint-budget fallback, strict replay
  rejection, missing required data, resource limits, cancellation and concurrency.
- The library-only target builds without GUI, JsonCpp, Python or ecCodes
  dependencies. The comparison client requires its documented extra packages.
- All 91 protected first-experiment/shared source fingerprints still match.
  See [the scoped code review](CODE-REVIEW.md) for coding checks and limitations.

The useful change is architectural: one continuing search can use compact ocean
fronts, finer coastal steps, cached shoreline hints and deferred alternatives.
The results do **not** establish that geographic switching alone makes it better.
The next worthwhile experiment would store deferred alternatives more compactly
and improve the choice of coastal approaches, then measure those changes
separately with the available controlled variants. Increasing all allowances
would obscure the current failure mode.

The earlier 74-route report remains a separate campaign. Its counts and timings
must not be combined with these suites, providers and deadlines.
