# Initial Linux qualification — 7 November 2026

This records the first local x86-64 Linux build of 2.0.0, based on 1.36
commit f2b78a7. It is separate from the stable plugin catalogue.

## Checks completed

- Release build against stock API 1.22 headers, retaining the API 1.21 host
  contract; pinned static Jasper; private staged installation and TGZ package.
- 623 enabled CTest tests passed. The existing live ocean timing comparison
  remains disabled. New checks cover independent GRIB loading, failed forecast
  replacement, frame ownership, bundled Climatology, dataset precedence,
  missing masks, worker queries and boat/polar import isolation.
- Private OpenCPN profiles with only version 2 enabled completed forecast-only,
  Climatology-only and combined-source routes. A passage extending beyond a
  three-hour forecast completed with reported Climatology use after expiry.
- A chart-aware calculation completed using a private copy of the enhanced
  OpenCPN core and the existing test CM93 collection.
- The packaged generator downloaded a small live NOAA GFS wind GRIB. The
  integrated reader loaded it; the new screen calculated an offshore passage.
- Interactive checks covered route editing, polar library access, request
  prefill, reports, clean shutdown and restoration of passage/weather selection.
- Hashes of the working launcher, working OpenCPN executable, working desktop
  entry and working 1.36 plugin were unchanged after testing.

## Measured size

The Linux archive is approximately **53.7 MiB**. Installed file contents total
**133.8 MiB**; filesystem allocation on this laptop is approximately **159 MiB**.
The private OpenCPN host, profiles, build directory and downloaded forecasts
are separate from those plugin figures.

| Component | Installed contents |
| --- | ---: |
| Plugin binary | 7.8 MiB |
| Climatology 2026.2 data and provenance | 10.4 MiB |
| Generator runtime, shared libraries and model definitions | 106.7 MiB |
| Other resources and generator executable | 8.9 MiB |

The working 1.36 installation contains 14.7 MiB of files, making the initial
version 2 installation approximately **119.1 MiB larger**. This comparison uses
the actual local builds, not an estimate for every target platform. Most growth
comes from the independent GRIB service. Unused BUFR, aviation and GRIB3
definitions are excluded. Dataset months are loaded lazily with a bounded cache;
installed bytes are not a measure of startup memory consumption.

## Remaining qualification

Windows and macOS dependency packaging, Android's generator adapter and touch
workspace, the corresponding CI builds, and broader passage/provider testing
remain before catalogue publication. This local validation does not qualify
those platforms or claim the complete UI redesign is finished. The desktop
generator and advanced editors retain parts of their existing interfaces.
