# Windows x64 Preview packages

The `windows-x64` CircleCI job builds both desktop identities from the same
revision: **xWeatherRouting 1.17.3.0** and **WeatherRouting 1.17.11.0**. These are
the desktop 1.17.11 implementation. Android 1.17.12 is a separate development
branch and is not an input to these packages.

Use these packages with the native **OpenCPN Windows x64 Preview**, built with
wxWidgets 3.2.8. Their metadata uses `msvc-wx32-x64`, Windows target version `10`,
and architecture `x86_64`. The ordinary Windows x86 job remains in the matrix.

The x64 job uses the native host import library and zlib SDK recorded in
`ci/windows64-sdk.json`. The archive contains its source revision, dependency
hashes and licences. The SDK export checks the import libraries and zlib DLL
are AMD64; the plugin build verifies the archive and every recorded checksum.
The wxWidgets development headers, libraries and runtime are official 3.2.8
x64 archives pinned by SHA256. Replacing the SDK requires another native host
build and a recorded update of its archive and manifest.

The build uses a disposable source checkout. Its retained patch contains the
same installed icon/data lookup adjustment used by the Preview bundle and
Windows temporary-file paths for tests. The routing engine is unchanged.
Tests run for both package identities. Each archive must contain its selected
AMD64 plugin DLL, match its version/target metadata, exclude test libraries, and
pass the compact GSHHG Crude/Low/Intermediate checksum checks. Native pointer
truncation warnings are treated as build errors.

CircleCI retains each archive, metadata, test report, architecture information,
source and SDK provenance, and SHA256 values under `windows-x64`. Package
compilation and tests do not by themselves qualify graphics drivers, charts,
or navigation devices; the complete Preview workflow additionally checks all
six plugins together, the chart-safety connection and profile isolation.

## Qualified build, 19 September 2026

[CircleCI workflow 1b122679](https://app.circleci.com/workflow/1b122679-31dc-439f-85e5-c557ce41d953)
passed all ten platform jobs at source revision
`156ef142a11330327f32a45b8883f9f0d70d8736`. Job 608 built xWeatherRouting
1.17.3.0 and WeatherRouting 1.17.11.0 for Windows x64; each passed all 302
registered tests. Both downloaded package/XML pairs matched their recorded
SHA256 values, AMD64 manifests, version metadata and compact shoreline hashes.

The matching [complete Preview run](https://github.com/pob220/OpenCPN-Chart-Aware/actions/runs/35443540536)
passed its native core/plugin tests and complete runtime check with Celestial
Navigation 2.8.8.0. It verified all six bundled plugins, the chart-safety
connection, profile isolation, 140 AMD64 images and 58 operational dataset
files. The separate xWeatherRouting and Celestial DLLs match those in the ZIP.

This x64 job is part of the normal validation matrix on
`release/xweather-alpha-1.17.3`. Its packages are Preview build artifacts;
Windows x64 catalogue publication is a separate operation.
