# Eskiu migration status — Phase 3 readiness

The repository-side migration program is at the promotion boundary.

## Repository foundation complete

The fork now has:

- language-neutral engine ABI and version/capability query;
- React/TypeScript engine-contract facade;
- pinned Eskiu v0.9.2 host ABI proof;
- Apple target probes;
- Android AArch64/JNI/AAR/16 KB packaging proof;
- WebAssembly object/link proof;
- generic measurement and named subsystem metrics;
- input, spatial, surface, media, geometry and world-mesh contracts;
- C++/Eskiu differential geometry coverage;
- bounded input-ring Eskiu shadow coverage;
- surface, media and world-mesh Eskiu shadow backends;
- normalized same-device C++/Eskiu benchmark artifact format;
- portable XR semantic conformance;
- profiling gates for physics, particles, post-processing, glTF, world mesh and media.

The repository rollout stack is now landed. The backend selector, accessor-materialization ABI,
Eskiu differential shadow, and production C++ glTF adapter are all on `main`. No public JSX
redesign was required.

## What "complete" means at repository level

Repository completion is now achieved: every production promotion prerequisite that can be
implemented without per-device evidence is present:

1. stable ABI;
2. C++ reference behavior;
3. Eskiu shadow/differential behavior;
4. rollback/backend-selection mechanism;
5. instrumentation;
6. reproducible benchmark schema;
7. CI target/build validation;
8. public API compatibility;
9. glTF accessor transient-buffer contract + Eskiu differential shadow + C++ production adapter.

It does **not** mean an unmeasured renderer subsystem is switched to Eskiu by default.

## Production promotion gate

Each domain must independently provide all of the following before AUTO may resolve
to Eskiu in a release build:

- ReactVision native/platform MCP mapping for the exact implementation path;
- same-device C++ baseline artifact;
- same-device Eskiu artifact;
- correctness/visual/event-order parity;
- meaningful measured memory or allocation benefit;
- no unacceptable frame-time/input-latency regression;
- platform matrix relevant to that domain;
- feature-flagged soak;
- rollback switch retained.

## Candidate promotion order

1. bounded input / pose storage;
2. dynamic geometry buffer preparation;
3. world-mesh chunk processing;
4. external surface bookkeeping;
5. media frame lifecycle/bookkeeping;
6. isolated glTF accessor/transient materialization;
7. other asset transient stages selected by measurements;
8. physics/particles/post-process only when profiling justifies a language migration.

## Explicit non-goals

- rewriting TypeScript/Zustand orchestration in Eskiu;
- moving device targeting/config plugins into the renderer;
- exposing Metal/OpenGL/WebGPU objects through the universal ABI;
- replacing GPU shader code merely to claim an Eskiu migration;
- changing public React component props as part of a backend swap.

## Promotion evidence record

Use `docs/eskiu-production-promotion-gates.json` for machine-readable status.
A domain remains `blocked-on-evidence` until every required item is attached to
its production-promotion PR.
