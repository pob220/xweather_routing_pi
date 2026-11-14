# 2.3.1 development qualification

14 November 2026. Based on qualified [2.3.0](QUALIFICATION-2.3.0.md).

## Visibility regression

A private GTK/X11 OpenCPN host test reproduces the clarified report: a routing's eye is enabled while another row is selected. It draws the actual overlay into both a wxDC bitmap and a Mesa OpenGL canvas and checks the endpoint pixels at projected coordinates.

The test covers before calculation, a running real-GRIB calculation, and its completed validated route. Each stage checks the visible route selected, another route selected, no selection, and the eye off with either route selected: **30 rendering checks** in total. The previous 2.3.0 build failed the 12 unselected/visible checks. The fixed build passed all 30, including normal host shutdown. The test also verifies the renamed Routing methods page.

```sh
python3 tools/test-route-visibility-linux.py --build BUILD --prefix PREFIX \
  --host /path/to/opencpn --xvfb /path/to/Xvfb --output NEW_OUTPUT_DIRECTORY \
  --opengl
```

As with the reset host test, the default forecast is the bundled deterministic real GRIB. `--forecast` and `--departure` allow a separate real-forecast replay. The instrumented test library is private and never installed or packaged.

## Other checks

- All **717 enabled Linux tests passed**; the existing ocean timing comparison remains disabled.
- All **175 frozen routing implementation inputs** remain unchanged.
- The reset/start race, completed reset/recalculation, Cancel, queued edit guards and captured selection/deletion tests still pass with the renamed reset confirmation.
- The arm64 Android build passed. TLS isolation and 16 KiB ELF alignment pass for the release library and stripped package. All 1,013 imported wx symbols resolve against both the tablet's 5.15 host and the pinned stock 5.14 host. No new physical-tablet or macOS runtime qualification is claimed.
- [Native Windows run 43649689656](https://github.com/pob220/xweather_routing_pi/actions/runs/43649689656) passed for wxWidgets 3.3.4 and 3.2.9 at source `e07a0a96ce5d74737cc0138c51028093b1337f9a`. Each build passed **708 enabled tests**, matched-host routing, and normal-profile Plugin Manager installation/reinstallation.

## Installation and delivery

The existing 2.x test installation and its original desktop shortcut now launch 2.3.1. A smoke test of that actual launcher mapped the installed release library and closed normally. All 732 boat, polar and forecast files, navigation-table counts and database integrity were preserved. The working OpenCPN host, wrapper, shortcuts, production Weather Routing 1.36.3 and xGRIB libraries were checked unchanged.

The attached SM-X210 tablet was backed up, then updated through the existing ADB deployment workflow with the host stopped. The exact stripped package library cold-loaded as 2.3.1; Plugin Manager version/import/file records were updated to match. Its hash is `4975199333722e4cc229baaab8261a1945b122d1f45c3a70c64110d5db73b1dd`. All 733 protected user files, passage settings, positions and navigation-table counts were preserved. The five other plugin libraries, including xGRIB and Celestial Navigation, are unchanged.

This is a cold-load and preservation check, not a claimed Android Plugin Manager UI import or a new visual-tablet qualification. The display was locked; programmatic keyguard dismissal was not performed. Onscreen checks remain pending manual unlock. The original power setting is restored after deployment.

Rick's deliverable is a Windows x64 Plugin Manager `.tar.gz` for wxWidgets 3.3.4; no bundled OpenCPN or installer is required.

Both developer-test tarballs are in [the 2.3.1 download](https://github.com/pob220/xweather_routing_pi/releases/tag/developer-test-2.3.1-20261114):

- Windows wxWidgets 3.3.4 x64 SHA256: `be07cb4b6ca52e98be66f256576555af1d08aa9c209a9f3206a192376cf60975`.
- Android arm64 import SHA256: `49b86ffb5f7c71886355f191a8b4e2bebb6655b07d3233d891e6f23255e181ea`.
