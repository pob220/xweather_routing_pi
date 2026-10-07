# xWeatherRouting 1.27 HostApi123 review consumer

This branch starts from released 1.27 (`51be6df`) and changes only the optional
chart-safety integration boundary to the developing HostApi123 interface.
The routing engines, Android controls, cancellation/deadline behavior, atlas
planning and ordinary route behavior remain from 1.27.

It uses the coordinated `opencpn-libs` API123 snapshot. Published API1.22 is the
loading floor; new chart functions belong to HostApi123. These headers and the
matching development host must be reviewed together. This is a review branch,
not a new plugin release, package upload or catalogue change.

Build with `-DWEATHER_ROUTING_STANDALONE_API=ON -DOCPN_BUILD_TEST=ON`.
Run CTest from the build test subdirectory. The unit stub intentionally supplies
no enhanced host; it verifies fallback behavior and cache/engine contracts.
Enhanced runtime and cross-platform qualification are separate checks.
