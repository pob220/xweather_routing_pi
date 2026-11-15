# xWeatherRouting 2.3.2

This maintenance pass fixes concrete weather-service and diagnostic problems
found while investigating issue #37. It does not change routing algorithms,
search allowances, automatic adaptive policies, route acceptance or safety limits.

* The internal GRIB reader acknowledges each pending request before publishing
  its answer. Previously the request could remain pending and the same frame
  was repeatedly constructed and published. Acknowledging after publication
  would risk clearing a new request from the awakened worker.
* A real cache miss wakes the GUI's weather-service timer promptly, while valid
  and unavailable-frame cache hits do not generate notifications. The idle timer
  cadence and existing cache memory guards are retained.
* Auto and All record each invoked method's elapsed wall time and returned work
  counters, including a cancelled method. Wall time includes service waits;
  it is not a CPU-time or allocated-memory measurement.
* Cancellation clears the delivered route but preserves earlier returned work
  and the last observed worker progress. Sampled progress and potentially
  incomplete returned counts are labelled explicitly.
* Diagnostic inputs now show whether automatic adaptive settings were enabled.
  The configured time and heading steps are distinguished from automatic choices
  made on calculation-local copies.

The shared servicing and reporting fixes are also in production 1.36.4. Its
external-provider path already acknowledges requests correctly, so it does not
need the internal-reader correction.

## Qualification

On 15 November 2026 all **725 enabled Linux tests passed**. The existing ocean
timing comparison remains disabled. Tests include real positive/negative cache
hits, consecutive asynchronous wakeups, pre-cancelled requests, accounting across
method changes, winning-route counters, final cancellation and sampled reporting.

The frozen calculation footprint retains its original manifest. The check permits
one exact reporting-only adapter substitution: cancellation keeps diagnostics
while clearing route output. Reversing that substitution must reproduce the
original adapter hash; all other 174 files must match unchanged. Solver sources,
limits and adaptive calculation inputs remain unchanged.

Private OpenCPN 5.15 profiles verified:

| Control | Result |
| --- | --- |
| Real Irish Sea GRIB, Standard, without xGRIB | Complete; fingerprint `857383926234c8c9`, 14,383 generated states, ETA 28 September 2026 01:30:53 UTC. Same route and counters as the production 1.36.3 control. |
| Expired GRIB with permitted Climatology fallback, Atlas | Same bounded-search failure and counters as 2.3.1; 908 distinct unavailable frames, 908 publications and no timeouts. The old reader published 4,787 times for those 908 frames. |
| All, Quick2 then Professional, stop during Professional | Quick2 completed; both invocation timings and 5,180,140 reported generated states survived cancellation. Separate last worker sample retained; no delivered route accepted. |
| Halmstad to north Scotland, Quick2, Climatology | Same fingerprint `9b74448e09f8b018`, 594.329 NM, ETA 18 November 2026 03:51:27 UTC. |
| North Scotland to Killybegs, Standard, departing at that ETA | Same fingerprint `5569526aebf5158a`, 498.594 NM, ETA 21 November 2026 04:00:08 UTC. |

The waypoint pair changes the geographical routing problem and is an invariance
control, not a claim that the original unsplit Halmstad passage is solved or
optimal. Currents were off and wave coverage was unavailable in these Climatology
controls. Wall times from concurrent qualification runs are not benchmarks.

The Android arm64 plugin cross-build also passes. This does not claim new tablet
runtime qualification. No working host, test installation, tablet or catalogue
has been updated by this maintenance pass.

[Native Windows run 43748134444](https://github.com/pob220/xweather_routing_pi/actions/runs/43748134444)
passed for wxWidgets 3.3.4 and 3.2.9 at source
`0c4bc1ff59ced7d5a19ab27664e716125c9ffabe`. Each configuration passed 716 enabled
tests, matched-host imports, normal-profile Plugin Manager installation and
reinstallation, shoreline routes and packaged Climatology/fallback routing checks.

The Windows x64 wxWidgets 3.3.4 Plugin Manager tarball is published as a
[developer test](https://github.com/pob220/xweather_routing_pi/releases/tag/developer-test-2.3.2-20261115),
not an alpha or catalogue update. Its SHA256 is
`61f58170b992dc02191eae6af4250e4e792d536a12a9161ec2482326f2359463`.
The reset/queued-edit and route-visibility GUI probes also pass on Linux,
including normal shutdown. Both production and development hosts additionally
closed cleanly during an active calculation.
