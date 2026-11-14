# 2.3.0 development qualification

14 November 2026. This is a UI development pass based on qualified 2.2.6, not an alpha or catalogue release.

## Preserved inputs and behaviour

All 175 entries in `testdata/engine-freeze-2.2.sha256` verify unchanged. No routing kernel, numerical adapter, adaptive planning policy, search allowance or weather-provider implementation was changed. Existing menu commands, shortcuts, route exports, optional chart/depth safety, standalone forecast loading/generation and xGRIB handoff remain in their existing implementations.

Desktop settings reuse the existing widgets and event bindings. Android retains the existing seven touch sections and Plan / Routes / Results / Tools workflow. The new desktop results window reuses the existing plot, statistics and report controls and restores their ownership before plugin shutdown. Existing multi-selection reports/statistics remain available.

## Linux checks

- All **717 enabled tests passed**. The existing `LiveRoutingPreview.OceanTimingComparison` test remains disabled.
- A private OpenCPN 5.15 GUI profile exercised all five desktop settings pages at 760 by 650 pixels, and both main pages at 720 by 740 pixels. The endpoint page scrolls, settings/actions remain reachable, and export actions wrap.
- Host contracts verify critical control ancestry, page switching without configuration changes, error recovery to the correct page, zero selected engines, unchanged manual values and the Auto/All masks. Mouse and stale numeric events cannot unlock managed controls. The automatic/manual contract retains a 5,370-second step and 7.25-degree manual heading interval.
- Reversed sailing-angle edits are rejected before the existing normaliser changes the saved range. The fields, footer and readiness checks explain the correction. The last valid range remains saved.
- A standalone real-GRIB calculation from 53.4 N, 5.5 W to 53.4 N, 4.8 W completed in Auto with Quick. The accepted passage used **12,219 seconds of forecast wind and zero Climatology wind**. All six results pages and Resources were opened after completion. The table summary was checked against the accepted arrival time, including the final approach.
- Results passage navigation, clearing table/plot references, destruction/recreation of the grouped window, and normal host shutdown passed. Empty plots show a message instead of uninitialised axis values.
- The uninstrumented installed library separately completed a real-GRIB route and the workspace, engine and adaptive GUI contracts. The visual harness uses a private instrumented copy; that copy is excluded from installation and packages.

The new explicit host check is enabled only with `WR_HEADLESS_WORKSPACE_CONTRACT=1` alongside the existing disposable headless scenario inputs. It deliberately mutates and restores a test route; use an isolated profile.

## Android build checks and limits

The arm64 binary builds against the existing qualified wxQt/Qt/NDK dependencies. TLS isolation and 16 KiB ELF alignment pass. All imported wx symbols resolve against both the actual tablet's 5.15 host and the pinned stock 5.14 host. The import package retains metadata, bundled Climatology, shoreline assets, CA material and dependency licences.

This pass does **not** claim a new physical-tablet test of 2.3.0 or a native Windows/macOS runtime test. The tablet remains on its qualified 2.2.6 installation. Prior 2.2.6 qualifications are background evidence, not substitutes for these outstanding platform tests. Production 1.36.3 and the working laptop installation remain separate.
