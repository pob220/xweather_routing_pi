# Standalone MIM + coastal A*: second experiment

This is a second, separately named C++20 experiment. The first engine in
[`../mim-astar`](../mim-astar/) and all six existing comparison engines are
unchanged. This folder is not registered with the OpenCPN plugin build.

[Testing instructions](QUINTON-TESTING.md) give the headless build and replay
commands. [Results](RESULTS.md) records what the tests actually establish.
[The per-route breakdown](breakdown/README.md) adds coloured timing matrices,
grouped medians and means, and discussion of Auto/All and chart-aware refinement.

## What is different?

The first experiment tries MIM over the voyage, then starts A* again at
departure. This experiment keeps **one sailing history** while changing how
it explores possible next moves:

1. In uncomplicated water, advance compact MIM fronts: possible positions at
   the same time, retaining different directions and boat states.
2. Near land, extend those same histories with smaller steps and a priority
   queue of possible approaches. There is no restart from departure.
3. As a branch returns to open water, it can use larger MIM steps again.
4. Keep less promising alternatives as deferred work. Revisit them during
   coastal exploration or when the active search stalls.

A coastal expansion first tries a small batch of promising headings. The
unfinished part of that fan remains a compact work record referring to its
parent. Its other headings can be calculated later, without first creating
every possible child. The ranked fan is cached once and remains in the same
order when resumed. This avoids silently skipping directions as guidance
becomes available.

The default returns the first independently validated completion. The
`mim_coastal_improve` comparison mode continues under the remaining overall
allowance. There is no compulsory half-and-half split or 512-expansion
post-solution cap. Finding a route does not prove it is the fastest possible.

## Cheap use of installed GSHHG

The standalone input can name an existing OpenCPN-format shoreline file, or
use `"shoreline": "auto"` with `tools/prepare_case.py`. Discovery checks
known OpenCPN data directories and the repository's shoreline directory. Both
legacy `poly-f-1.dat`-style names and versioned `.dat.gz` names are supported. It
selects the finest supported available resolution and records the resolved path and
hash. Compressed data is expanded beside the prepared case, never in the
installed data directory. The raw benchmark reader needs an uncompressed file.
Discovery does not download, replace or change installed files. Raw upstream shapefiles
and other unrelated GSHHG formats are not decoded by this helper.

In the library API, callers can pass an already loaded GSHHG provider through
`Options::geographyGuidance`. By default, the supplied safety provider is also
used for hints; the original environment remains authoritative.

Two bounded hints control the search effort:

- **Cached proximity:** reuse a shoreline-distance estimate for nearby
  positions, allowing for their distance from the cached sample. The coastal
  band includes a limited lookahead based on the nominal sailing step and the
  configured land margin. Classification follows candidate positions, not
  just sections of a preselected straight route.
- **A rough detour map:** lazily build a small grid around the passage when
  coastal work is needed. Water connections suggest a remaining distance and
  useful direction around obstructions. Interpolation avoids artificial cost
  jumps at grid-cell boundaries. Predicted moves into coarse blocked cells
  get a worse ranking, rather than being permanently excluded.

The defaults allow 768 map cells, 6,000 guidance-provider accesses and 4,096
cached proximity cells. The cache evicts old samples when full. If its query
allowance runs out, new unknown locations use normal checked MIM rather than
being labelled coastal; blocked movement can still trigger finer search. A caller can explicitly use a coarser file as
`guidance_shoreline` while keeping a finer `shoreline` for motion checks.

**The map is a hint, not permission to sail.** A narrow channel may disappear
from it, a small island may be absent, or a useful detour may leave its area.
Outside or unresolved parts fall back to direct-distance guidance. All
actual sailing, motoring, waiting and final route replay still use the
original providers. An intentionally incorrect open-water hint is included
in the tests to check this separation.

GSHHG provides shorelines, not charted depths or complete reef information.
The supplied headless GRIB client does not become CM93/chart-aware by using
this map. Chart/depth qualification needs the corresponding real providers.

## What kind of A* is this?

Coastal work is ordered by time already used plus an estimated remaining
time. The geographic estimate is approximate and **can overestimate** the
best remaining journey; it is not an admissible heuristic. This is a guided,
bounded A*-style experiment, not a textbook optimality certificate.

A separate optional `certifiedMaximumGroundSpeedKnots` supplies a conservative
arrival-time lower bound. Only that bound can reject a candidate for being
unable to improve an already validated route. Zero disables this bound; it
does not disable the geographical ranking. Observing a speed above the
certificate aborts delivery. The caller must justify the bound for all allowed
currents and propulsion states, not merely observed samples.

Rounded coastal position/heading cells retain a small set of primary histories,
including histories already explored. Excess choices are deferred. This avoids
repeatedly exploring nearly identical arrivals. Exact times, sail/motor state and complete physical histories
remain stored. Deferred alternatives and unfinished heading fans can be
revisited, but finite work/storage limits can still prevent success. The
algorithm does not promise to solve every feasible passage.

## Defaults and resource limits

The initial controls are experimental, not production recommendations:

| Control | Default |
| --- | ---: |
| Ocean heading spacing / sectors | 10 degrees / 24 |
| Coastal heading spacing / nominal step | 5 degrees / 15 minutes |
| Maximum physical integration slice | 5 minutes |
| Coastal headings calculated in one batch | 8 |
| Deferred service period during coastal work | 16 scheduling turns |
| Motion attempts / stored physical labels | 120,000 / 40,000 |
| Managed search-container memory | 64 MiB |

Request limits can further reduce these allowances. Provider caches, result
legs, strings and bounded guidance-map allocations are not all charged to the
PMR search allocator. Its peak is **managed search bytes**, not total process
RAM; the comparison client also reports process RSS. Synchronous provider
calls must themselves be bounded: this engine cannot interrupt a provider
which never returns.

Only deterministic minimum-time objectives and endpoints within 85 degrees
of latitude are supported. Ordinary resource exhaustion can retain a valid
incumbent in improvement mode. Cancellation, a violated speed certificate or
authoritative validation escalation rejects delivery, including an incumbent.

## Source layout and preservation

- `include/mim_coastal/Router.h`: named public options, units and counters.
- `src/Router.cpp`: persistent histories, ocean/coastal scheduling and replay.
- `src/GeographyHints.h`: request-local, bounded geographic hints.
- `src/SearchStorage.h`: allocator, state competition and queue records.
- `tests/Contracts.cpp`: physical, geography, acceptance and lifetime checks.
- `tools/`: input discovery/preparation and unchanged-baseline comparisons.

Physical integration, final connection and acceptance support were adapted
from our first experiment; the search controller is different. Both use the
same unchanged shared routing libraries. This is deliberate code reuse, not
a claim of source-isolated clean-room development. File SPDX headers and the
repository licences apply. No FairlySoon or Windy implementation is imported.

`baseline.json` fingerprints the protected first experiment and four vendor
trees. Run `python3 experiments/mim-astar-coastal/tools/check_baseline.py` to
verify those files. The benchmark's preparation and independent external
replay are outside its engine calculation timer; failed solves remain in
calculation medians. See the results for the exact source and timing scope.
