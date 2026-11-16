# Try xWeatherRouting 2.3.2 on Debian or Apple Silicon

These developer-test tarballs include `metadata.xml` for OpenCPN's built-in
Plugin Manager. Download the archive for the host's operating system and
architecture, then choose **Options → Plugins → Import plugin**, select the
intact `.tar.gz`, enable xWeatherRouting and restart if requested.

* [Debian 12 (Bookworm), x86_64](https://github.com/pob220/xweather_routing_pi/releases/download/developer-test-2.3.2-20261115/xweather_routing_pi-2.3.2-debian-x86_64-12-bookworm.tar.gz)
* [Debian 13 (Trixie), x86_64](https://github.com/pob220/xweather_routing_pi/releases/download/developer-test-2.3.2-20261115/xweather_routing_pi-2.3.2-debian-x86_64-13-trixie.tar.gz)
* [macOS, Apple Silicon (arm64)](https://github.com/pob220/xweather_routing_pi/releases/download/developer-test-2.3.2-20261115/xweather_routing_pi-2.3.2-darwin-wx32-arm64-15.3.2-macos-arm64.tar.gz)

The Mac package is for wxWidgets 3.2 hosts, and was qualified on native arm64
macOS 15.3.2. It is not an Intel or universal package. The Debian packages use
each distribution's native wxWidgets 3.2 libraries.

All three passed native tests, stock OpenCPN Plugin Manager import/reinstallation,
shoreline/offshore routing and relocated offline GRIB generation. They include
the generator's scientific dependencies and data. See
[qualification and exact source revisions](RELEASE-NOTES-2.3.2.md#unix-package-qualification).

These remain developer tests, not alpha or catalogue releases. Version 2 uses
separate preferences and personal data from production version 1. See
[the 2.3.2 changes](RELEASE-NOTES-2.3.2.md) and use
**View → Routing progress → Run record** to copy or save feedback.
