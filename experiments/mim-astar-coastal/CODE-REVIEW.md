# Coding and maintenance review

This is a scoped review of the new experiment, using the
[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
It is not certification that every rule is satisfied, nor a review of all the
unchanged vendor libraries.

The public `Router`, `Options`, `Diagnostics` and `Result` types give the main
operations and units explicit names. `SearchSession` owns each request's search;
`GeographyHints` isolates approximate geographic advice; `StatePartition` and
`BudgetMemoryResource` isolate state grouping and allocation accounting. Classes
protect internal state; structs carry configuration and results. This follows
the distinction in C.1–C.3, rather than requiring classes for every data item.

RAII containers and smart pointers manage ownership. Borrowed request/provider
references last only for the synchronous call. Allocator identity and geography
objects cannot be copied or moved. There is no mutable global search state.
Configuration ranges are checked at the API boundary; duration types and named
distance/speed options clarify the interface (I.1, I.4, I.5; R.1).

Comments explain the reasons for the unusual parts: retaining previously
expanded primary histories, resuming partial heading fans, separating geographic
ranking from certified bounds, charging explicit waiting, aborting on authority
escalation and replaying reconstructed legs. Method names describe operations;
short coordinate indices are confined to local grid arithmetic. The README and
test guide explain the search in plain language before discussing its controls.

Validation performed:

- GCC Release builds with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` on the
  new library, with no warnings from its source. Existing shared libraries still
  produce their pre-existing warnings and were not edited to hide them.
- clang-format 22 formatting checked on the new C++ source and headers.
- clang-tidy 22: analyzer checks, use-after-move, owning-memory, member
  initialization and slicing checks; no diagnostics in this scope.
- Eight focused regression groups in Release and under ASan/UBSan; leak
  detection disabled for the sandboxed sanitizer run.
- Library-only build, strict-JSON client smoke tests, Python compilation,
  shoreline preparation/discovery and baseline fingerprint checks.

Important limits remain explicit: the coarse geographic priority is approximate;
full optimality is not claimed. PMR counters cover managed search containers,
not all process memory. Provider calls must return in bounded time. Unsupported
offline weather/profile/chart features are not silently treated as supported.
The shared provider API represents angles and distances as doubles; units are
documented rather than introducing incompatible wrapper types in this experiment.

Physical helpers and the offline preparation adapter reuse our first experiment's
implementation. The first engine and vendor sources are unchanged. This is not
a claim of a source-isolated clean-room implementation; SPDX headers and the
repository licences apply. No FairlySoon or Windy code was imported.
