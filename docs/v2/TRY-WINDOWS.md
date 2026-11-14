# Try xWeatherRouting 2.3.0 on Windows

This developer test is a **Plugin Manager import tarball** for Windows 64-bit
OpenCPN using wxWidgets 3.3.4, matching Rick’s OpenCPN 5.15 build.

1. [Download the 2.3.0 Windows 64-bit plugin](https://github.com/pob220/xweather_routing_pi/releases/download/windows-plugin-test-2.3.0-20261114/xweather_routing_pi-2.3.0-msvc-64-x86_64-10.0-MSVC-x64-wx334.tar.gz).
2. In OpenCPN, open **Options → Plugins → Import plugin** and select the downloaded
   `.tar.gz` file. Keep the archive intact.
3. Enable xWeatherRouting and restart OpenCPN if requested.

See [the 2.3.0 changes](RELEASE-NOTES-2.3.0.md) and
[qualification and remaining platform limits](QUALIFICATION-2.3.0.md).
This is a developer test, not an alpha or catalogue publication. Version 2
retains separate preferences and user data from the production version 1 line.

Plan passage and Routes remain. Desktop settings are grouped into Route, Boat,
Weather, Safety and Engine; detailed results have coordinated views. Existing
routing engines, automatic adaptive settings, forecasts, polars, reports,
exports, multi-leg passages and optional chart safety are preserved.

Settings for an active passage are read-only. Wait for completion or stop the
computation explicitly in Routing progress before changing them. Reset
confirmations recheck the selected passages after the prompt closes. An accepted
reset clears obsolete computed results so the passage can be calculated again.

To see the departure triangle and destination cross, select the route and enable
its eye in Routes. A visible, unselected route shows its track without those
endpoint symbols. Bring both endpoints into view and move any covering windows.

For feedback, **View → Routing progress → Run record** has **Copy record** and
**Save record**. Include the route, settings, weather inputs and steps that led
to the result. Review records before sharing because they contain positions
and filenames.
