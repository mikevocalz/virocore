# Eskiu input runtime backend

This track advances the input domain from a single-thread shadow proof to a
runtime-selectable backend while preserving the existing C++ reference ABI.

## What is implemented

- `experimental/eskiu/VROEngineInputEskiu.esk` is a lock-free SPSC ring.
- Eskiu v0.9.2 `<atomic>` intrinsics provide acquire/release
  `atomic_load` / `atomic_store` operations for the queue indices.
- The queue keeps the frozen 96-byte `VROEngineInputSample` layout.
- Push/pop allocate nothing after queue creation.
- `VROEngineInputBackend` resolves the `input` domain through the shared backend
  selector and dispatches to C++ or Eskiu.
- `AUTO` still resolves to C++.
- Explicit `eskiu` selection succeeds only in binaries that actually link the
  Eskiu input object.
- The host conformance test exercises both backends and runs 100,000 samples
  through the Eskiu queue with a real producer thread and consumer thread.

## Why 32-bit atomic indices

Eskiu v0.9.2 exposes atomics on `int` cells. The implementation therefore stores
read/write sequence counters in 32-bit atomic cells and interprets them as
wrapping `uint32` values. Capacity is restricted to `< 2^31`, so the standard
wrapping-distance SPSC full/empty test remains unambiguous.

The dropped counter is also a 32-bit atomic cell and saturates at `INT32_MAX`.
The public ABI continues to return it as `uint64_t`.

## Rollout status

This closes the **atomic SPSC repository gate** for the input storage candidate.
It does not change the production default: device A/B latency/memory evidence and
the platform soak gate are still required before `AUTO` may prefer Eskiu.
