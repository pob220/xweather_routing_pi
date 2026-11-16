# xWeatherRouting 2.3.3

One **Passage shoreline** resolution now applies to every routing method,
including Auto and All. Existing mixed method choices migrate to their finest
explicitly saved level. Subsequent manual choices, including lower levels, are
remembered. Old XML keys are mirrored for reopening in previous plugin versions.
Missing optional data never cause an automatic downgrade.

The separate **Scout shoreline resolution** is preserved when chart/depth
enforcement is active. Routing algorithms, numerical controls, adaptive policies,
search allowances and chart/depth acceptance rules are unchanged.

* Dataset choices show all five levels and their availability. Installing Full
  and selecting Full for a passage are explicitly separate actions.
* The planning summary shows the passage's selected shoreline. Readiness names
  a missing selected dataset and offers **Install selected shoreline data**.
  Coarser shoreline-only choices warn that small islands can be absent; the
  warning does not block a deliberate choice. A coarse chart scout does not
  generate that shoreline-only warning.
* Completed results show the shoreline identity recorded during that
  calculation, including each distinct identity across linked legs. The run
  record retains the prepared dataset's resolution, GSHHG version, SHA256 and
  file path. Later setup edits do not rewrite that recorded identity.
* **View → Show selected passage shoreline** draws the actual selected GSHHG
  outlines in purple, including islands absent from the displayed background.
  The labelled snapshot survives hiding the workspace. Toggle it off/on to
  inspect another setup. Android has the action in **Tools → Display and safety**.
* Outline inspection excludes tile-clipping edges, caches each viewport and
  bounds work to 16 tiles / 30,000 segments. Wider or denser views request a
  closer zoom; geometry is not simplified to fit the display allowance.
* Context help distinguishes routing GSHHG from OpenCPN 5.10+'s OSM background
  display. The latter is updated through Chart Downloader's OSM basemap
  catalogue. Existing forecasts, polars, route editing, analysis and export
  capabilities remain available.

Qualification includes the complete Linux suite, XML/preferences migration and
older-format compatibility, Full-GSHHG crossing and outline probes, GUI edits
across every method and Auto/All, missing-data readiness, settings persistence,
hidden-workspace rendering and clean host shutdown. The real Irish Sea GRIB
control retains its exact track, ETA and search settings
(`857383926234c8c9`, 28 September 2026 01:30:53 UTC). The Android arm64 cross-build
passes; this is not a claim of new tablet runtime qualification.

These checks do not reproduce Quinton's original Tonga passage; his actual GPX,
configuration and diagnostics are still needed for that investigation.
