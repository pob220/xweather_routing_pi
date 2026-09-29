# GRIB reader and record hardening — review sources

These are **standalone review candidates**, dated 29 September 2026. This
folder does not change production plugin source, installations or published
packages. No plugin build should take these files by discovery or include path.

## Files to review

The 13 matching revised files are in [`revised/`](revised/). Twelve are changed
or new. `GribVectorPolicy.h` is included unchanged to match its revised `.cpp`.
`GribRecord.cpp` and `.h` are the **unchanged** record pair qualified in the
prior review; the remaining sources extend that work through the reader,
GRIB1/GRIB2 decoders and compressed-file handling.

The reader and decoder files belong in the **GRIB provider** (xGRIB or OpenCPN's
bundled GRIB plugin). Weather Routing normally uses only the shared
`GribRecord.cpp` and `GribRecord.h`. `GribVectorPolicy` is specific to xGRIB.
Merge provider-specific differences before adopting these files and rebuild
matching headers and implementations together.

## Review material

- [`REVIEW-NOTES.txt`](REVIEW-NOTES.txt): contracts, changes, limits, testing,
  performance, remaining acceptance work and build instructions.
- [`FINAL-RESULTS.txt`](FINAL-RESULTS.txt) and
  [`FINAL-RESULTS.json`](FINAL-RESULTS.json): qualification summary and hashes.
- [`reader-hardening-xgrib-baseline.patch`](reader-hardening-xgrib-baseline.patch):
  checked patch against the exact sources in `baseline-reader/`.
- [`GribRecord-weather-routing-baseline.patch`](GribRecord-weather-routing-baseline.patch):
  checked patch against the two `GribRecord.before.*` files.
- [`SOURCE-SHA256SUMS`](SOURCE-SHA256SUMS): hashes for the reviewed files.

The complete standalone test fixture and fuzz corpus archive remains in Paul's
Documents and is available separately. The repository folder contains the
reviewed sources and evidence summary; it does not include the crash
reproducers or 3,000-plus corpus files. The earlier record-only Windows/Wine
qualification applies to the shared record pair, not the new parser files.

This review does **not** certify the older bundled JasPer 1.900.1 codec. Its
wrapper was bounded and Android compatibility tested, but the codec itself is
an external dependency and requires separate review or replacement.
