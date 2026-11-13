# xWeatherRouting 2.2.0

This release focuses on the planning interface and diagnostic records. All
production engine sources, dispatch policies, resource policies, vessel inputs
and default routing configuration are unchanged from 2.1.3. Experimental search
work is confined to an independently built executable under `tools/engine-lab`.

* Replace lengthy always-visible configuration notes with section Help buttons,
  short tooltips and F1 help. Help is available offline by click/tap and keyboard.
  Active readiness problems and current safety status remain visible.
* Make both desktop configuration tabs scrollable and responsive to narrow
  windows. Preserve settings and planner geometry, including secondary monitors;
  clamp windows back onto an available display after monitor changes.
* Provide an explicit wind/wave reset with a preview and Cancel. It preserves
  route, boat, weather-source choices, engine settings and chart-safety settings.
  The existing search-preset reset has a separate, explicit label.
* Remove the redundant collapsed pane inside Routes and analysis. Add an
  Analysis tools menu linking to the existing route table, reports, statistics,
  plots and run diagnostics.
* Retain bounded run records for the session: actual input snapshots, observed
  stage transitions and final engine diagnostic reports, including stopped runs.
  Finished elapsed time remains fixed. Copy or save records for comparisons.
  Interface-observed stage times are explicitly distinguished from solver times.

No engine is added to Auto/All, no heading or time-step defaults change, and no
solver failure is reclassified. The standalone Atlas policy experiment documents
its route-quality tradeoff rather than changing the production search.
