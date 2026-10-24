# Does the date-line issue affect installed xWeatherRouting v1.34?

**A related date-line depth problem is confirmed in the installed v1.34 plugin/core combination. The raw API timeout was not reproduced in the matched normal-routing tests.**

All tests were automated in disposable OpenCPN profiles and separate network namespaces, using the same core and v1.34 library selected by the working launcher, local CM93 charts, real GFS weather and a Lagoon 560 polar. The latitude was **18.77°S**, with a **0.1 NM land margin**. The nearby control ran from **179.94°E to 179.95°E**; the crossing ran from **179.99°E to 179.99°W**.

| Installed v1.34 test | Result | Headless test phase | Whole test app |
|---|---|---|---|
| Nearby control, depth 1.5 m | Complete | 4.61 s | 10.55 s |
| Date-line crossing, depth 1.5 m | Failed | 4.49 s | 10.40 s |
| Same crossing, depth disabled (diagnostic) | Complete | 4.55 s | 10.55 s |

The headless phase includes the scout and chart preparation; these are not pure solver timings. None of the three tests reached its 60-second watchdog. All protected working-file hashes remained unchanged.

With 1.5 m depth required, the crossing was rejected after the chart mask reported **unknown depth at −180°**, including a sample at **18.7675°S, 180°**. The native Quick2 search itself returned in about **36 ms** after preparation. Removing only the depth requirement in the disposable diagnostic allowed the same crossing to complete, with about **41 ms** of native search. This identifies a seam-depth limitation rather than a demonstrated search stall in this test. The generic UI suggestion to increase Max Diverted Course did not explain the logged depth rejection.

v1.34 already splits date-line segments before host calls and canonicalizes tile longitudes. Its route workers normally use the plugin-owned chart-hazard evaluator, and prewarming builds an unwrapped longitude envelope. The direct external chart API bypasses that wrapper. In the separate raw-API experiment, **6 of 16 requests returned HTTP 504 after about 10 seconds**; that observation must not be generalized to all v1.34 routing. Other cold-cache, review or long-passage paths have not been cleared by these three small tests.

The depth-disabled run only isolates the fault. It does not qualify a route under the original 1.5 m condition. A targeted fix should address depth evidence at the longitude seam and its clearance halo, preserve fail-closed handling of unknown depth, invalidate affected cached tiles, and then repeat the full Tonga–Fiji chart-aware tests. This experiment does not establish whether the missing depth originates in CM93 coverage or extraction.

The benchmark reader’s missing global-GRIB seam was a separate harness issue; an exact sample-preserving regional crop corrected it. No working plugin, core binary or user setting was changed.

Implementation evidence: [ChartLongitude.h](https://github.com/pob220/xweather_routing_pi/blob/7156844dec9ab1bccb826fa787ac426af17b5fbd/include/ChartLongitude.h), [ChartSafetyHost.cpp](https://github.com/pob220/xweather_routing_pi/blob/7156844dec9ab1bccb826fa787ac426af17b5fbd/src/ChartSafetyHost.cpp).

Tested library SHA-256: `3c793031a9e87f17eda98d7b8f00616b78989b9ce09ab563ca33499d38d8b68d`. Core SHA-256: `0409d6a76db92ee50064280469049459880969290107436b6206ef9501ddddd3`.
