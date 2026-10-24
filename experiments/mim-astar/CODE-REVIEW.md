# C++ code review

This review covers the independently written standalone library in `include/`
and `src/`. It uses the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines),
manual inspection, compiler warnings, clang-tidy, contract tests and recorded
route replays. It is not a certification that every guideline, or the unchanged
shared dependency tree, has been mechanically proved to conform.

## Changes for human readers

The public `Router.h` now explains every option group, units, valid ranges,
managed-memory scope, callback lifetime and the difference between MIM guidance
and a certified A* bound. Defaults have names and readable digit separators.
Each diagnostic has its own declaration. `StateKey` and `SectorKey` have named
fields rather than opaque positional tuple types, while retaining the original
comparison order. `retainMimProposal` isolates the frontier-selection rule.

Comments describe the reasons for the two retained MIM alternatives, fresh A*
restart, lossy cell pruning, numerical endpoint/time allowances, transactional
incumbent acceptance and cancellation semantics. They avoid narrating obvious
assignments. These changes apply the intent of NL.1–NL.4 and NL.7–NL.8 without
renaming the shared API solely to impose a different naming convention.

## Ownership and failure handling

The engine uses caller-owned providers and one scoped `SearchSession` per route.
`BudgetMemoryResource` cannot be copied or moved, preventing allocation counters
from being duplicated while containers still refer to the original resource.
Search storage is destroyed before the next stage begins. `StatePartition`
copies the three configuration values it needs, rather than retaining an
unnecessary reference to the complete options object.

The normal MIM-to-A* handover now uses an explicit return/status path, addressing
E.3. Exceptions remain for cancellation, exhausted resources and other failures;
the private error type derives from `std::exception`. Callback exceptions remain
isolated and counted, and cannot accept an unvalidated route. Invalid strategy
enum values are rejected at the API boundary and have a regression check.

References in `SearchSession` are deliberate borrowed dependencies of a
noncopyable controller. The narrowly documented C.12 lint suppression applies
only to those members. Replacing them with nullable raw pointers would weaken
their contract. The standard memory-resource interface's `void*` overrides are
required by that interface; they do not transfer resource ownership to callers.

## Reproducible checks

Headers explicitly include their dependencies. The review script compiles each
of four new headers in a separate translation unit, checks formatting, and runs
the configured `cppcoreguidelines-*`, `bugprone-*`, `performance-*`, identifier
length and include checks on the library. Baseline diagnostics are outside the
owned-code filter; that is a scope boundary, not a claim to have fixed them.

From the repository root after configuring/building the experiment:

```sh
python3 experiments/mim-astar/tools/check_code.py --build build-mim-astar
ctest --test-dir build-mim-astar --output-on-failure
```

clang-format and clang-tidy are additional review dependencies. The Release
contracts and the prototype/import ASan/UBSan groups pass. LeakSanitizer is
disabled for the recorded sandbox run because process tracing prevents it from
operating. Recorded scenario outcomes, arrival times and complete tracks are
also compared against the frozen pre-review results; the verification receipt
identifies that source/build and any differences.

The benchmark measurements in `QUINTON-SUMMARY.md` remain measurements of the
frozen comparison builds. This readability review does not claim a performance
improvement or a production use case. Approximate pruning, finite work limits,
sampled controls and provider limitations remain explicit parts of the design.
