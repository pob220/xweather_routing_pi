# xWeatherRouting 1.30.0 HostApi123 review consumer

This branch includes the qualified 1.30.0 release (`df1726599482`) and retains
the optional chart-safety integration through the developing HostApi123
interface. Its routing engines, polar policies, motoring, coastal endpoint
handling, configured Quick timesteps and bounded Professional reconstruction
are the same source as the 1.30.0 release. The API adaptation is separate from
those routing changes.

It uses the coordinated `opencpn-libs` API123 snapshot. Published API1.22 is the
loading floor; new chart functions belong to HostApi123. These headers and the
matching development host must be reviewed together. This is a review branch,
not a new plugin release, package upload or catalogue change.

Build with `-DWEATHER_ROUTING_STANDALONE_API=ON -DOCPN_BUILD_TEST=ON`.
Run CTest from the build test subdirectory. The unit stub intentionally supplies
no enhanced host; it verifies fallback behavior and cache/engine contracts.
Enhanced runtime and cross-platform qualification are separate checks.

The canonical review branch is `chart-safety/xweather-1.30-api123`. The preceding
`chart-safety/xweather-1.27-api123` branch is also advanced to this source so older
review links select the updated consumer. The published `xweather-routing-alpha`
branch and 1.30.0 packages retain their API 1.21 loading floor and existing
optional chart-service bindings; this development consumer uses API 1.22 as
its loading floor and the coordinated API 1.23 development header.

Local qualification of this update: the full plugin and test target build;
all 484 routing/cache/contract tests and all 15 packaging tests pass. All
routing-engine, polar, route and GUI sources match released 1.30.0. The
HostApi123 declarations match the core proposal, ignoring whitespace. These
unit tests model absent enhanced capability; they do not establish real-host
chart routing or cross-platform integration.
