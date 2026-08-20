# External-control planning provider (Preview B)

Preview B lets a hardened OpenCPN host discover xWeatherRouting as
`route-planning.chart-weather.v1`.  The adapter is deliberately optional:
it resolves the host registration functions at runtime.  Stock OpenCPN hosts
which do not export them log one informational message and retain every normal
xWeatherRouting GUI feature.

The native OpenCPN plug-in API remains version 1.21.  No virtual method,
plug-in class layout, or existing ABI contract is changed.

## Request boundary

The provider accepts coordinates, departure time/window, concurrency, routing
effort, minimum depth, land margin, an installed polar file name, and explicit
climatology fallback.  Preview B intentionally supports only the datasets
currently active in xGRIB/current providers.  It rejects paths as polar
identities and never imports returned geometry into OpenCPN automatically.
The host independently revalidates a completed route against authoritative
chart safety before exposing it as a draft.

Climatology fallback is fail-closed.  It is disabled when the field is absent
or false and maps to xWeatherRouting's existing most-likely mode only when
`allowClimatologyFallback` is explicitly true.  This uses the established
Climatology messaging contract and does not depend on a particular compatible
Climatology build.

## Lifecycle and compatibility fixes

The implementation fixes four defects found while qualifying the resident
provider path:

1. A completed resident calculation retained headless-run state, making every
   later request report busy.  Completion cleanup now clears that state on the
   OpenCPN thread.
2. A delayed OpenCPN-thread start could run after its temporary scenario files
   had been deleted.  A timed-out start is marked abandoned and the delayed
   callback cannot begin it.
3. External jobs and GUI calculations could start concurrently against shared
   route-engine state.  Admission now requires no resident, running, waiting,
   multi-leg, or departure-optimisation work.
4. The API's climatology-fallback choice was discarded by self-contained
   scenario loading.  The scenario contract now preserves an explicit value;
   omission remains deterministic and fail-closed.

The first three are confined to the new resident-provider path and do not
affect stock OpenCPN.  The fourth also corrects reusable headless scenarios.
No xGRIB or Climatology defect was demonstrated during this pass.

## Unload and cancellation

Only one resident job is accepted at a time.  Core cancellation is forwarded
to xWeatherRouting on the OpenCPN thread.  Provider removal first disappears
from discovery and cancels active work; plug-in deactivation is vetoed until
the pinned job has drained.  This prevents callbacks into an unloaded shared
library without changing the legacy plug-in ABI.

## Qualification expectations

Before publication, both builds must pass:

- the complete xWeatherRouting test suite against its vendored stock 1.21 API;
- compilation against the hardened OpenCPN provider header;
- stock-host load behavior with the optional symbols absent;
- hardened-host registration, planning, cancellation, repeated-job, and
  unload/reload workflows;
- authoritative chart rejection for unsafe returned geometry.

Preview B is a developer preview.  Resource discovery, concurrent provider
jobs, broader dataset identities, and automatic route import are intentionally
deferred until this narrow lifecycle and safety boundary has field evidence.
