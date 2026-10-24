# Tonga routing comparison — 24 October 2026

The screenshots show **Lalona → Uoleva, Ha’apai**, a local passage of about **30.7 NM direct**. The destination is labelled Uolevai in the image, off Uoleva island. It is separate from the full Tonga–Vuda passage. We tested it separately from the saved **Pukutalo → Vuda Marina** route (488.8 NM direct) and a representative **Nuku’alofa harbour → Vuda Marina** route (470.0 NM direct).

**Automated CM93-aware results** — an isolated test OpenCPN supplied real chart classifications to the seven standalone C++ engines. A route counts as solved only after shared-physics chronological replay and a separate whole-track check through OpenCPN’s authoritative chart API at **1.5 m minimum depth / 0.1 NM clearance**.

| Engine | Pukutalo → Vuda | Nuku’alofa → Vuda | Lalona → Uoleva |
|---|---|---|---|
| MIM+A* | No within allowance | No within allowance | No within allowance |
| Quick2 | No within allowance | No within allowance | Yes — 1.14 s |
| Atlas | No within allowance | No within allowance | No within allowance |
| Quick | No within allowance | No within allowance | Yes — 7.38 s |
| Standard | No within allowance | No within allowance | Yes — 5.26 s |
| Alternative | No within allowance | No within allowance | Yes — 6.67 s |
| Professional | No within allowance | No within allowance | Yes — 17.92 s |

Times are **second-run routing CPU seconds**, using the second of two runs per cell, with common limits of **60 CPU seconds / 180 elapsed seconds after input loading**, plus each engine’s existing resource caps. Separate chart-server CPU, application startup and input loading are excluded from CPU time; waiting for chart data counts against the elapsed limit. Post-run independent audits are excluded from calculation times. This keeps chart-server waiting time from consuming the CPU allowance. No result is a proof of infeasibility or optimality.

**Coastline-only controls** used the same real weather, boat polar and Full GSHHG shoreline, with chart/depth checks disabled. These calculation times are elapsed seconds from separate cold runs:

| Engine | Pukutalo → Vuda | Nuku’alofa → Vuda | Lalona → Uoleva |
|---|---|---|---|
| MIM+A* | No within allowance | No within allowance | Yes — 0.13 s |
| Quick2 | Yes — 2.38 s | Yes — 3.19 s | Yes — 0.09 s |
| Atlas | Yes — 3.68 s | Yes — 2.49 s | Yes — 2.39 s |
| Quick | No within allowance | No within allowance | Yes — 0.60 s |
| Standard | Yes — 2.99 s | Yes — 3.91 s | Yes — 0.19 s |
| Alternative | Yes — 7.31 s | Yes — 7.29 s | Yes — 0.14 s |
| Professional | No within allowance | No within allowance | Yes — 1.49 s |

**None of the 15 completed coastline-only tracks passed the independent CM93 check.** Fourteen were rejected and one remained unresolved. A coastline-only completion therefore does not count as a chart-aware solution.

CM93 contains useful island, drying-area and depth detail here, and it supports independently accepted routes. The chart service also reports **unknown depth near 20.18°S, 174.80°W**, between Nomuka and Kotu; engines must avoid that area when depth is required. This test does not establish whether that gap comes from the chart data or its extraction. There is no demonstrated need to generate a replacement Tonga chart for this experiment.

Inputs and limits: all three cases use real **NOAA GFS 6 October 2026 06Z**, departing **6 October 18:00:01 UTC**, a **Lagoon 560 substitute polar** because Quinton’s Simonis 53 file is unavailable, sailing angles 40–160°, 120° search allowance, one-hour configured steps, and no currents or waves. The screenshot time is a displayed forecast frame, not a known departure time. Its endpoints were fitted from five island outlines: approximately **20.3501°S, 174.5415°W → 19.8474°S, 174.4452°W**, with an estimated **±0.25 NM** uncertainty. Nuku’alofa is a representative harbour-water start (**21.1280°S, 175.1760°W**); Vuda is the saved **17.713143°S, 177.353350°E** endpoint. These are controlled representative tests. The image-only reconstruction cannot validate Quinton’s original plotted route or reproduce his boat, charts, settings or weather exactly.

The chart-aware standalone measurements use a local test adapter and a compatible OpenCPN chart-safety core. The public standalone testing guide currently covers GRIB/GSHHG runs; it does not include this CM93 adapter. The whole-plugin integration checks use the existing automated scenario system.

The shared headless client explicitly integrates sailing legs; it does not reproduce the GUI’s implicit OptimizeTacking display or its separate MaxDivertedCourse envelope. CM93 was read through the local test core, without installing MIM+A* in xWeatherRouting. An exact sample-preserving regional GRIB crop avoids the benchmark reader’s global date-line seam; the initial runs affected by that harness issue were discarded.

| Independently accepted route | Engine | Sailing time | Distance |
|---|---|---|---|
| screenshot lalona uoleva | Quick2 | 32h 15m | 138.9 NM |
| screenshot lalona uoleva | Quick | 18h 57m | 89.4 NM |
| screenshot lalona uoleva | Standard | 32h 50m | 150.3 NM |
| screenshot lalona uoleva | Alternative | 60h 26m | 287.1 NM |
| screenshot lalona uoleva | Professional | 18h 57m | 89.4 NM |

**Wider-angle local follow-up** — Quinton’s pictured line initially heads southwest, which may require more than the saved passage’s 120° allowance. Only the local case was repeated at 180°; other inputs and limits were unchanged.

| Engine | Lalona → Uoleva at 180° |
|---|---|
| MIM+A* | No within allowance |
| Quick2 | Yes — 2.76 s |
| Atlas | No within allowance |
| Quick | No within allowance |
| Standard | Yes — 9.93 s |
| Alternative | Chart audit rejected |
| Professional | No within allowance |

**Installed v1.34 integration checks** — the six current engines were also run automatically inside real offscreen OpenCPN instances on the reconstructed local passage, with charts enforced, 1.5 m depth, 0.1 NM margin and a 90-second headless watchdog. These are cold whole-plugin tests using the host’s configuration and presets; their times include chart preparation and are separate from the common standalone CPU comparison.

| Installed engine | Result | Headless phase |
|---|---|---|
| Quick2 | Completed | 10.75 s |
| Atlas | No completed route within test | 20.54 s |
| Quick | Completed | 7.85 s |
| Standard | Completed | 8.97 s |
| Alternative | Completed | 11.34 s |
| Professional | Completed | 24.68 s |

All five completed installed-plugin tracks also passed a separate whole-route CM93 API check. MIM+A* remains outside the installed plugin. Cowboy has no equivalent current CM93 adapter in this comparison; its historical validation controls remain separate.

**Date-line impact on the actual plugin:** the matched installed-v1.34 tests did not stall, but a depth-enabled crossing failed on unknown depth at the seam while the same depth-disabled diagnostic completed. See [the v1.34 assessment](V134-DATELINE-ASSESSMENT.md). This limits interpretation of unsuccessful Tonga–Fiji runs; chart-service and data limitations are not proofs that an engine cannot solve the passage.

Default MIM+A* produced 0/3 independently accepted primary routes, with 0 exclusive completions. Its default 120,000-motion cap remains part of this prototype comparison; the results do not rule out other MIM+A* implementations or larger allowances.

**MIM capacity check:** raising its motion allowance from 120,000 to 1,200,000, with the same 60 CPU-second / 180 elapsed-second limits and other caps retained, returned no route in the three primary cases or the 180° local variant. The CPU limit stopped these runs after approximately 335,000–632,000 motions; this does not mean the full 1.2 million motions were exhausted. No incumbent route had been found before cancellation. A separate short offshore CM93 positive control completed successfully, confirming that the prototype can route with this chart adapter.

The 180° Alternative candidate passed the grid/physics replay but failed the additional whole-plotted-track CM93 margin check in both repeats. It is excluded from completed-route counts. Broadening the search angle did not improve the overall completion count here.
