# Engine measurement substrate

This is the common measurement vocabulary for C++ / Eskiu A/B work.

It is **disabled by default**. Production call sites use no-op macros unless a target
is built with `VRO_ENGINE_METRICS_ENABLED=1`.

The initial fixed counters are:

- allocation count / allocated bytes
- free count / freed bytes
- copy count / copied bytes
- measured call count
- elapsed nanoseconds

The substrate does not decide *what* to instrument. Each migration PR identifies its
module/fixture and uses these counters consistently on the old and candidate backend.

## Snapshot discipline

A benchmark record must also capture outside this struct:

- commit SHA
- device/runtime/platform
- workload fixture
- backend kind
- process/RSS peak where platform tooling can provide it
- frame p50/p95 where relevant

The in-engine snapshot exists for counts that cannot be reconstructed reliably from
external profilers.

## Release behavior

Default builds compile instrumentation macros to no-ops and the snapshot reports zeros.
No global allocation hooks are installed.
