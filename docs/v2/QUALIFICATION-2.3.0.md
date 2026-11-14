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

This pass does **not** claim a new physical-tablet test of 2.3.0 or a native macOS runtime test. The tablet remains on its qualified 2.2.6 installation. Prior 2.2.6 qualifications are background evidence, not substitutes for these outstanding platform tests. Production 1.36.3 and the working laptop installation remain separate.

## Windows and test installation

[Native Windows run 43645279088](https://github.com/pob220/xweather_routing_pi/actions/runs/43645279088) passed for both wxWidgets 3.3.4 and 3.2.9 at source `2211053be91dc9ff48d0f16ace3e5f663eb723b9`. Each build passed all **708 enabled tests**, matched-host routing checks, and normal-profile Plugin Manager installation and reinstallation. The unchanged scientific helper was reused only after an exact build-input/provenance comparison with qualified 2.2.6; the new plugin and packaged runtime were validated again.

Rick's download is only the wxWidgets 3.3.4 Windows x64 Plugin Manager tarball, with embedded metadata for version 2.3.0, `msvc-64`, `x86_64`, target version 10 and API 1.21. The PE machine is AMD64 (`0x8664`). Its SHA256 is `a3398dff88c42a86d6fc1ad4d2ae6c6de1bdf2aaee5739f2091de103b7d994c1`. See [Windows import instructions](TRY-WINDOWS.md).

The earlier Linux 2.x test profile and its existing desktop shortcut now launch 2.3.0. The actual launcher was checked in an isolated display: it mapped the release library and closed normally. All 732 boat, polar and forecast files and the navigation-table counts were preserved; database integrity passed. The working OpenCPN wrapper, host, production Weather Routing and xGRIB libraries were checked unchanged. This is a local test installation and a developer-test download, not an alpha or catalogue publication.

## Endpoint display report

A disposable OpenCPN GUI test rendered a real-GRIB route during calculation and after completion. With the route selected and its visibility eye enabled, both the blue departure triangle and red destination cross appeared at the projected endpoint coordinates. Turning the eye off produced no route overlay. With a completed route visible but unselected, its track remained visible and the endpoint symbols were omitted, matching the existing selection behaviour.

No endpoint rendering fault was reproduced, and no rendering code was changed. The report's cropped screenshot alone does not establish whether selection, visibility, windows covering the endpoints or chart extent explains the missing marks. First select the route, enable its eye and bring both endpoints into view.

## Engine-reset freeze regression

A disposable real-GRIB GUI test reproduced an unsafe reset against the initial 2.3 UI commit (`5956a4c`): confirmation opened with a start queued, computation began inside the modal loop, and Apply changed the live time step from 600 to 3,600 seconds. The interface blocked for 90,239 ms; the worker then reported unusable wind data and the route failed. An operating-system crash was not reproduced.

The settings guard covers queued starts, preparation, workers, alternative exploration and owned multi-leg work. It rejects stale edit events and checks captured reset targets again after confirmation. It does not stop a worker or apply a reset while routing is active. Completed-route resets clear UI references and obsolete results before applying the new settings.

The reusable GTK/X11 host test covers the confirmation/start race, successful completion without changing live settings, completed-result reset followed by recalculation, Cancel preserving results, queued resets and stale edits being rejected, restored controls after completion, and selection/deletion of captured targets. Run it against a built and installed development prefix:

```sh
python3 tools/test-settings-reset-linux.py --build BUILD --prefix PREFIX \
  --host /path/to/opencpn --xvfb /path/to/Xvfb --output NEW_OUTPUT_DIRECTORY
```

The default is the bundled deterministic real GRIB. `--forecast` and `--departure` permit a separate real-forecast replay. Test objects and profiles are private to the new output directory; the instrumented library is never installed or packaged.
