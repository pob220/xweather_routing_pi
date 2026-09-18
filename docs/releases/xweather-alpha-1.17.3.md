# xWeatherRouting Alpha 1.17.3

This Alpha release packages the later 1.17.11 desktop source as
`xWeatherRouting` 1.17.3.0. The catalogue version follows the previously
published xWeatherRouting 1.17.2.0; the source includes the intervening
1.17.3–1.17.11 fixes and improvements. The experimental Android 1.17.12
branch is excluded.

Since the 1.17.2 Alpha, the desktop plugin has gained stricter chart-policy
invalidation and wind-coverage checks, bounded coastal recovery, Main and
Quick routing engines, selectable shoreline detail, shared GRIB timeline
caching, responsive chart-safety preparation and the compact GSHHG package.
Crude, Low and Intermediate shoreline datasets are included. High and Full
can be installed through the shoreline manager when needed.

This is an Alpha release for testing against stock OpenCPN 5.14. The optional
chart-backed safety features still require a host that exposes the experimental
service; ordinary GSHHG land checks work on stock OpenCPN. As with the earlier
Alpha, disable standard Weather Routing before enabling xWeatherRouting and
back up routing configuration, boats and polars before upgrading.
