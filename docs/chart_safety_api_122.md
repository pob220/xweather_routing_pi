# OpenCPN API 1.22 chart-safety consumer

xWeatherRouting uses the draft `HostApi122` chart-safety service as an optional
capability. It obtains the normal host API object, checks for `HostApi122`, and
registers its identity-scoped semantic tile cache under the plugin's common
name. No chart-safety symbols are discovered with `dlsym` or
`GetProcAddress`.

If the running host exposes only `HostApi121`, initialisation reports the
enhanced capability as unavailable and the plugin continues with its existing
GSHHS/chart-independent routing paths. An API 1.22 host can additionally use
native vector charts, CM93 and registered licensed-chart providers.

This development branch keeps API 1.21 as its loadable minimum while compiling
against a provisional API 1.22 header pinned from `pob220/opencpn-libs`.
`HostApi122` is an abstract feature-detection interface, so it does not leave a
new API-1.22 RTTI symbol for an older host to resolve. The snapshot is temporary
and must be replaced by the official `OpenCPN/opencpn-libs` API 1.22 package
when the API is published. The standalone unit-test host supplies a null
`GetHostApi()` stub; integration and GUI tests use the real core
implementation.
