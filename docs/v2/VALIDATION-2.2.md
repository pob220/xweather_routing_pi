# 2.2 validation

The production baseline is 2.1.3 commit
`0eb63adb5db00c758fd18f9b502c77a4e68298d8`. CTest checks 164 frozen engine,
provider, headless, policy and configuration inputs against their SHA256 hashes.
The standalone lab has no plugin target, registration or Auto/All entry.

Linux Release validation on 2026-11-13:

* Plugin builds and installs into an isolated prefix.
* All 679 enabled Linux CTest cases passed. The new-passage preference tests and the private GUI checks verify Auto/all-six
  defaults, independent checkbox changes, the next route and host restart. The existing timing benchmark remains
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

[Native Windows qualification](https://github.com/pob220/xweather_routing_pi/actions/runs/43410521523)
passed on both wxWidgets 3.3.4 and 3.2.9 hosts: 670 enabled CTest cases each,
matched-host ABI/import, orderly shutdown, reinstall preservation, packaged
helper and forecast/Climatology/fallback calculations. Product source is
`c2218df518f671f335d01f5f6b38ed2fd0d510ac`.

[Portable UI qualification](https://github.com/pob220/xweather_routing_pi/actions/runs/43413555188)
passed fresh Auto/all-six checkbox defaults, settings geometry, click/tap Help,
Unicode labels, relocated paths,
forecast/Climatology/fallback routes and preservation of a normal profile.
Its automation source is `4fc6dd144722ee9cf35deae30d4aaacbe5ecdfd0`; only the
native selector-reading assertion changed after product qualification.
The delivered archive and every manifest file were checksum-verified again
locally. The archive SHA256 is `99370e2c6621b1beaf17f1af311f817d7084cc08f47651b7978456574e685b0b`.

The private Linux headless regression also rejected both coincident-leg cases
before routing, and completed both legs of a legitimate return passage. The unchanged
scientific helper is reused only after source/build-input and staged-source hash
checks; its packaged runtime is tested again with the new plugin.

This is not an Android UI qualification or a broad DPI/display-matrix test.
The synthetic lab results do not reproduce Rick's original passage and do not
justify promoting the first-valid policy or adopting issue #33's algorithm.
Production engine implementations, solver defaults, status classification and
weather constraints remain unchanged. Only new GUI passage selection defaults
use Auto/all-six; legacy route XML and headless scenario defaults are retained.
