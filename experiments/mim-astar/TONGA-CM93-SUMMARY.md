**Tonga / CM93 routing tests — summary for Quinton**

The screenshots show **Lalona → Uolevai, off Uoleva in Ha’apai** (30.7 NM direct). They do not show the full Tonga–Fiji passage. We tested that local reconstruction, the saved **Pukutalo → Vuda Marina** route (488.8 NM direct), and a representative **Nuku’alofa → Vuda** route (470.0 NM direct).

**Actual installed OpenCPN v1.34, local screenshot passage:** automated runs on private virtual displays, using our CM93 charts, 1.5 m minimum depth and 0.1 NM clearance. Every successful exported track also passed a separate whole-route CM93 check.

| Engine | Validated route | Calculation phase |
|---|---|---:|
| Quick2 | Yes | 10.75 s |
| Atlas | No in this test | — |
| Quick | Yes | 7.85 s |
| Standard | Yes | 8.97 s |
| Alternative | Yes | 11.34 s |
| Professional | Yes | 24.68 s |

Times are one cold headless run, including chart preparation and excluding app startup. The headless watchdog was 90 seconds.

**Standalone MIM+A*:** no completed route in any of the three primary chart-aware cases, and no exclusive solution. It also failed the 180° local variant. Raising the motion allowance from 120,000 to 1,200,000 still returned no route within the same 60 CPU-second limit. A simple chart-aware offshore positive control passed. MIM+A* remains outside the installed plugin.

**Long passages:** none of the seven standalone engines returned an independently accepted chart-aware solution within 60 CPU seconds / 180 elapsed seconds after input loading. Some runs reached preparation or search limits. These results do not establish infeasibility. The [full comparison](TONGA-TEST-DETAILS.md) includes tables for all three passages, coastline-only controls and the wider-angle follow-up.

**CM93 is useful for this test:** it supports five validated local routes. Unknown-depth areas still force detours. No need for a replacement Tonga chart has been demonstrated.

**Actual v1.34 date-line finding:** the matched normal-routing tests did not stall, but requiring 1.5 m depth rejected a crossing because depth was unknown at the seam. The same crossing completed in a depth-disabled diagnostic. The raw external chart API separately showed timeouts. See the [v1.34 assessment](V134-DATELINE-ASSESSMENT.md).

All cases used real NOAA GFS 6 October 2026 06Z wind, departing 6 October 18:00:01 UTC, with a Lagoon 560 substitute polar. Quinton’s Simonis 53 file was unavailable. Screenshot endpoints are approximate (estimated ±0.25 NM), so this is a representative comparison, not an exact replay of his original route. No working installation or user setting was changed.
