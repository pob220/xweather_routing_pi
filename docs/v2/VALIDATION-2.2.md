# 2.2 validation

The production baseline is 2.1.3 commit
`0eb63adb5db00c758fd18f9b502c77a4e68298d8`. CTest checks 164 frozen engine,
provider, headless, policy and configuration inputs against their SHA256 hashes.
The standalone lab has no plugin target, registration or Auto/All entry.

Linux Release validation on 2026-11-13:

* Plugin builds and installs into an isolated prefix.
* The full enabled CTest suite passes; the existing timing benchmark remains
  disabled. Geometry, record retention, fixed completion clocks, copied input
  snapshots, stopped outcomes and bounded long-run history have focused tests.
* A private X display, private D-Bus and disposable OpenCPN profile exercise the
  actual plugin: narrow settings layout, F1 context help, removal of verbose
  static notes, geometry after reopen and host restart, reset Cancel and Apply,
  preservation of route/vessel/weather/engine/safety settings, a completed
  Climatology route, retained input/result text, frozen elapsed time and reopening
  the run record from Analysis tools.
* Screenshots of the compact settings, help and finished record were inspected.
  The profile, plugin data and host are separate from the normal installation.
* The separate Atlas policy lab executes eight cases, two policies and three
  repeats. All 36 complete results pass independent chronological replay.
  Twelve expected non-completions remain non-completions. See the lab's README
  and raw JSON for timing and passage-quality tradeoffs.

Native Windows qualification uses both wxWidgets 3.3.4 and 3.2.9 hosts and tests
both the build and packaged runtime. The portable UI trial additionally tests
settings geometry and click/tap Help through native window controls. These runs
must pass before Windows release qualification is claimed. The unchanged
scientific helper is reused only after source/build-input and staged-source hash
checks; its packaged runtime is tested again with the new plugin.

This is not an Android UI qualification or a broad DPI/display-matrix test.
The synthetic lab results do not reproduce Rick's original passage and do not
justify promoting the first-valid policy or adopting issue #33's algorithm.
No production engine behavior, status classification or weather constraints
were changed to make the tests pass.
