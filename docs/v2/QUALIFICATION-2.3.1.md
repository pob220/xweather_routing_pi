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
- Android build and native Windows qualification are recorded below when complete. No new physical-tablet or macOS runtime qualification is claimed.

## Installation and delivery

Only the existing 2.x test installation is to be updated. Production Weather Routing 1.36.3, the working OpenCPN wrapper and xGRIB installation remain separate. Rick's deliverable is a Windows x64 Plugin Manager `.tar.gz` for wxWidgets 3.3.4; no bundled OpenCPN or installer is required.
