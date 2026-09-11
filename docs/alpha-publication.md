# xWeatherRouting Alpha publication

This follows xGRIB's documented, proven CircleCI/Cloudsmith procedure. With the
owner's approval, the upload job reuses the existing `xgrib-deployment` context;
xGRIB's repository, context contents/settings and packages are not changed.

## Status, 11 September 2026

The official `OpenCPN/plugins` Alpha catalogue has no xWeatherRouting entry.
The standalone source includes Weather Routing 1.17.1's route-table lifetime fix.
The earlier green CircleCI run at `9d55de5` is not release evidence for standalone
packages: its Linux containers built the standard WeatherRouting identity.
The default now selects xWeatherRouting without relying on CircleCI's legacy
repository-name environment variable, and desktop archive checks handle both
identities. Fresh hosted builds must pass before publication.

All nine jobs passed at `1e168fc`, and downloaded artifacts confirmed the corrected
standalone identity. However, full publication preflight rejected the Windows
tarball: Google's default `INSTALL_GTEST=ON` had included test DLLs, headers and
CMake package files. GoogleTest installation is now disabled for fetched test
dependencies, with an explicit Windows archive guard and regression test.
Do not publish the Windows archive from `1e168fc`; require the corrected rerun.

The final metadata audit also found two existing xGRIB fixes missing here:
Linux Docker must receive `BUILD_ENV`, `WX_VER` and `BUILD_GTK3` (particularly
Ubuntu 22.04's `ubuntu-wx32-x86_64` ABI), and Flatpak metadata must use
`flatpak-<architecture>`, not `flatpak-32-<architecture>`. The same fixes are
now applied to xWeatherRouting without editing xGRIB. Publication preflight
rejects these legacy labels.

## Credentials and destinations

- Public Cloudsmith raw repository: `pob220/xweather-routing-alpha`.
- Existing CircleCI context: `xgrib-deployment`, containing the working
  `CLOUDSMITH_API_KEY`. The key must allow uploads to the new repository; this
  must be verified during publication, not assumed. No secret retrieval or
  rotation is required. Do not copy credentials into source, commands or logs.
- Ordinary validation has no deployment context. Only the approval-gated upload
  job accesses it. The destination is fixed to xWeatherRouting's own repository.
- Do not change the CircleCI plan, add paid capacity, or bypass an approval gate.

## Procedure

1. Run the default `validate` workflow and check all nine targets. Debian 12/13,
   Ubuntu 22.04/24.04, Debian ARM64, Flatpak x86_64/aarch64, Windows x86 and native
   Apple-Silicon packages must have the standalone identity. macOS is ARM64,
   despite the historical `macos-universal` job name. Flatpak is a build/package
   check, not a claim of the same native CTest coverage or desktop validation.
2. Record the exact validated source commit and release tag without moving an
   existing tag. Launch `run_workflow_deploy=true` on that commit. If API access
   is unavailable, use xGRIB's documented isolated publication branch, changing
   only that parameter's default to true; never merge that switch into the
   normal branch.
3. The publication workflow rebuilds all nine targets and then pauses at
   `hold-for-alpha-approval`. Review the new results before approving it.
4. `deploy-alpha` uses a shared workspace and the existing context. It checks
   all nine archive/XML pairs before uploading anything, rejecting wrong names,
   standard plugin libraries, incomplete matrices, mixed versions and ambiguous
   Flatpak metadata. It retains resolved XML, an upload manifest and SHA-256s.
5. Check Cloudsmith independently: eighteen completed objects, the intended
   version/architecture, publicly downloadable URLs, matching archive checksums.
   A green uploader alone is not proof that asynchronous processing finished.
6. Add the resolved metadata to a branch of `pob220/plugins` based on the current
   upstream Alpha branch, generate/validate the catalogue using its tools, and
   open a PR against **OpenCPN/plugins:Alpha**. Uploads do not make a plugin appear
   in the official catalogue until that metadata change is merged.
7. Test catalogue installation in a disposable stock OpenCPN profile. Do not
   alter the user's running OpenCPN, enable Expert mode, or overwrite its config.

Stock API 1.21 hosts use GSHHS land checks. Chart/depth-aware routing needs the
optional enhanced host service; installing this plugin alone does not add it.
Standard WeatherRouting and xWeatherRouting share routing settings/user files:
back them up and enable only one variant at a time, or use isolated profiles.
Native GUI tests with real charts, weather and vessels remain necessary.
