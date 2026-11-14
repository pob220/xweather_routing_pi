# Try xWeatherRouting 2.3.1 on Android arm64

1. [Download the Android arm64 plugin tarball](https://github.com/pob220/xweather_routing_pi/releases/download/developer-test-2.3.1-20261114/xweather_routing_pi-2.3.1-android-arm64-16-import.tar.gz).
2. Keep the `.tar.gz` intact. In compatible 64-bit Android OpenCPN, open **Options → Plugins → Import plugin** and select it from Downloads.
3. Enable xWeatherRouting and restart OpenCPN if requested.

This is a developer test for the API 1.21 Android host family, not an APK, alpha or catalogue publication. Version 2 retains its separate preferences, boats, polars and weather data. The existing Android touch workflow remains; the desktop five-page arrangement is not imposed on the tablet. The former Engine section is labelled Routing methods.

The exact package library was deployed through ADB to the attached SM-X210 and cold-loaded successfully. User files, passages, navigation and the other plugins were verified preserved. A fresh Plugin Manager UI import and onscreen checks await an unlocked display; they are not claimed as passed.

See [changes](RELEASE-NOTES-2.3.1.md) and [qualification](QUALIFICATION-2.3.1.md). SHA256: `49b86ffb5f7c71886355f191a8b4e2bebb6655b07d3233d891e6f23255e181ea`.
