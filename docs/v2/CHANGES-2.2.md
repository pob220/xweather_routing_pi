# xWeatherRouting 2.2.1

* Explain disabled calculation with **Cannot calculate**, the exact blocking
  setting, a blocker count and an action beside it. Keep warnings separate.
* Offer **Turn off Draw exclusions for this passage** when a saved passage
  requires an unavailable Draw provider. Apply to all linked legs, preserving
  land avoidance, other passages and saved preferences. Passage checks offer
  the same action, with blockers listed before warnings.
* Mark Draw exclusions optional and show provider availability in Passage
  settings. Fresh defaults remain Draw off, land avoidance on, Auto with every
  engine checked; explicit saved choices remain intact.
* Expose **Try example passage** when the route is unset. The example uses
  bundled boat/Climatology data and clears inherited Draw/depth requirements.
* Clarify that unavailable Climatology fallback is a warning when forecast
  wind exists at departure; later coverage is still checked during routing.

Production engines and resource policies remain unchanged. Native exclusion
area creation/editing is outside this patch; Draw integration remains optional.

# xWeatherRouting 2.2.0

This release focuses on the planning interface and diagnostic records. All
production engine sources, dispatch policies, resource policies, vessel inputs
and solver defaults are unchanged from 2.1.3. Experimental search
work is confined to an independently built executable under `tools/engine-lab`.

* New passages use Auto with all six engines checked for both Auto and All.
  Saved mode and checkbox choices carry forward to the next route and survive
  restart. Existing routes keep their own settings.
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

No new engine is registered, no heading or time-step defaults change, and no
solver failure is reclassified. The standalone Atlas policy experiment documents
its route-quality tradeoff rather than changing the production search.
