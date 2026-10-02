# glTF / GLB memory instrumentation

This PR establishes a baseline before any Eskiu experiment in the asset-loading path.

## What is measured

When `VRO_ENGINE_METRICS_ENABLED=1`:

- each accessor materialization records requested transient bytes;
- base accessor copies record copied bytes;
- sparse accessor overlays record copied bytes;
- each background glTF/GLB parse/load records call count and elapsed nanoseconds.

The default release behavior is unchanged because the metrics macros compile to no-ops unless explicitly enabled.

## Why this comes before migration

`VROGLTFLoader` already contains upstream memory-copy fixes. We should not assume rewriting the loader in Eskiu will help. The initial experiment should target whichever transient stage is measurably dominant.

## Benchmark fixture

Use at least:

- small static GLB;
- large textured GLB;
- animated/skinned GLB;
- sparse-accessor asset;
- model with morph targets.

Record:

- peak process memory;
- `allocated_bytes`;
- `copied_bytes`;
- `call_count`;
- `elapsed_ns`;
- load success and scene parity.

The first Eskiu asset experiment must be smaller than the full loader: one isolated transient-buffer/materialization stage behind the existing C ABI.
