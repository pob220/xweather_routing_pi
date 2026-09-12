# xWeatherRouting 1.17.4 Alpha publication

This isolated publication branch builds all nine targets from the unchanged
1.17.4 runtime at a74ed8764cb7e1ef5668f606617e4d3677e2c831. It carries forward
the successfully used 1.17.2 publication fixes: catalogue summary length,
Flatpak and Ubuntu ABI labels, bounded HTTPS dependency bootstrap, and the
fixed Cloudsmith destination `pob220/xweather-routing-alpha-oss`.

The ordinary nine-platform validation and GitHub native Windows job at a74ed87
already passed. This branch runs a fresh complete publication build, then
stops at `hold-for-alpha-approval`. Review all nine successful jobs and artifact
pairs before approving that gate. Do not bypass the gate or reuse older
version binaries. Only `deploy-alpha` receives the already configured
`xgrib-deployment` context; no credential or xGRIB repository changes are needed.

After deployment, verify all 18 Cloudsmith objects complete processing, check
public downloads against CI checksums and validate every XML against the
catalogue XSD. Update existing OpenCPN/plugins PR 1406 through its current
pob220/plugins branch. Preserve its seven-target scope: Debian 12/13 x86_64,
Ubuntu 22.04/24.04 x86_64, Flatpak x86_64, Windows x86 and macOS ARM64.
Linux/Flatpak ARM64 remain direct-testing downloads pending device validation.

The deployment-default switch belongs only on this isolated branch. Do not
merge it into the regular source branch. Installing catalogue entries in the
official Alpha catalogue still requires maintainer review/merge of PR 1406.

## Debian 13 bootstrap recovery

Publication workflow e028b273-3360-4a51-8611-600b86c086dc built from 01877da7e4c0c7d414ef29ced0c443e00bab756a hit a Debian 13 HTTPS CA discovery failure before compilation.
A disposable Debian 13 reproduction failed with the original configuration
and succeeded with APT's documented explicit CAInfo path. TLS peer and host
verification remain enabled. The recovery workflow rebuilds only Debian 13,
verifies unchanged runtime/package sources against 01877da7e4c0c7d414ef29ced0c443e00bab756a, and collects the
other eight exact successful 1.17.4 builds. Per-file revisions, workflows and
checksums accompany deployment. The collector rejects failures or mismatched
provenance and still pauses at a separate approval gate before uploading.
