# ViroCore language-neutral extension architecture

Status: Phase 0 / Phase 1
Branch: `plan/eskiu-extension-architecture`
Paired repository: `mikevocalz/viro`

## Purpose

Prepare this ViroCore fork for incremental C++/Eskiu coexistence without coupling custom XR features to an unpublished Eskiu toolchain or ABI.

This document governs native implementation boundaries. The paired Viro document governs public React/TypeScript behavior.

## Hard rule

**Do not add Eskiu source, compiler flags, ABI declarations, allocator shims, or build steps based on inference.**

The ReactVision native/platform MCP must be queried first for the current ViroCore/Eskiu integration details. Until then:
- C++ is the reference implementation;
- all new boundaries are language-neutral;
- C-compatible data layout is preferred where a binary boundary is required;
- internal C++ abstractions may remain idiomatic C++ as long as they do not leak across the boundary.

## Current fork areas that need isolation

The custom/native work in this fork currently touches areas including:

- `VROSceneRendererOpenXR` and OpenXR runtime capability negotiation;
- `VROInputControllerBase` and precision input sampling;
- PICO/OpenXR support;
- visionOS renderer and Metal-facing paths;
- Web/WASM renderer sources;
- co-location/session paths;
- dynamic geometry and world-mesh related code;
- model loading and memory-sensitive asset paths;
- native panel/surface integrations carried by the paired React bridge.

These are not all Eskiu candidates. They are all **boundary candidates**.

## Boundary design principles

### 1. Opaque handles

Cross-language resources are represented by opaque integer or pointer-sized handles whose concrete representation is backend-private.

Never expose:
- `std::shared_ptr`;
- STL containers;
- C++ class layouts;
- virtual tables;
- backend-native objects.

### 2. Explicit lifetime

Every owned resource has a documented creator and destroy/release operation.

Borrowed data:
- has a call-scoped lifetime;
- cannot be retained;
- must document alignment and mutability.

### 3. POD wire structures

Boundary structures should use:
- fixed-width integers;
- floats/doubles;
- plain arrays;
- pointer + length views only when lifetime is explicit;
- version/size fields when forward compatibility matters.

### 4. Capability discovery

No backend is assumed to implement every domain.

Capabilities cover at least:
- renderer backend;
- OpenXR;
- AR tracking;
- spatial anchors;
- shared frames/co-location;
- world mesh;
- controller;
- hand tracking;
- gaze;
- stylus;
- camera texture;
- recording;
- external surfaces;
- dynamic geometry;
- Web/WASM.

### 5. Thread declaration

Each operation must be classified:
- renderer-thread;
- platform-main-thread;
- worker-safe;
- asynchronous;
- high-rate lock-free/latest-value ingestion.

### 6. No hidden allocator crossing

Memory allocated by one runtime is released by that same runtime unless ReactVision's verified Eskiu ABI explicitly specifies otherwise.

## Proposed logical domains

This is a **logical** contract map, not yet a frozen binary ABI.

### Runtime / diagnostics

Responsibilities:
- contract version;
- backend identifier;
- capability set;
- build/provenance identifier;
- diagnostics counters;
- structured status codes.

### Resource registry

Responsibilities:
- opaque handles;
- retain/release if shared ownership is required;
- typed resource kind;
- stale-handle detection in debug builds.

### Frame clock

Responsibilities:
- monotonic timestamp;
- frame index;
- predicted display time where available;
- begin/end markers;
- late-update stage where supported.

### Input stream

Sample types:
- controller pose/buttons;
- hand joints;
- pinch;
- gaze ray;
- stylus pose/buttons/pressure where supported;
- touch/mouse for web parity.

High-rate samples should avoid allocation and unbounded queues.

### Spatial stream

Data:
- rigid poses;
- anchors;
- shared coordinate transforms;
- planes;
- world mesh chunks;
- tracking quality/state.

### Buffer / geometry stream

Data:
- vertices;
- indices;
- attributes;
- partial update ranges;
- mesh identity/version;
- immutable vs mutable ownership.

### Surface / texture exchange

Data:
- producer type;
- dimensions/format;
- synchronization token;
- texture/render-target handle;
- frame availability;
- release/acquire semantics.

Used by:
- GPU panel;
- Rive panel;
- Three.js panel;
- camera texture;
- video/recording.

### Asset loading

Responsibilities:
- source stream;
- decode buffers;
- image bytes;
- mesh buffers;
- cancellation;
- peak-memory instrumentation.

## First implementation milestone

Before any Eskiu backend:

1. Add native contract version + backend diagnostics.
2. Add C++ reference implementation.
3. Add contract conformance tests.
4. Instrument allocations/copies in candidate hot paths.
5. Route one low-risk capability query through the contract.
6. Route one high-rate input stream through the contract.
7. Route one surface/texture producer through the contract.
8. Verify no public React API change.
9. Benchmark old vs contract-routed C++ path.
10. Only then begin backend substitution.

## Candidate order for backend substitution

Priority is based on expected memory/copy pressure and isolation value, not novelty.

1. input/pose sample storage;
2. dynamic geometry update buffers;
3. world-mesh chunk processing;
4. camera-frame buffers;
5. external panel/surface texture exchange;
6. asset-loader transient buffers;
7. Web/WASM tracking/math buffers;
8. post-process scratch data;
9. particles/physics transfer data if profiling justifies it.

Do **not** migrate:
- platform UI/config code;
- JS state;
- Expo config;
- feature policy;
- code whose measured bottleneck is GPU shader time rather than CPU allocation/ownership.

## Benchmark protocol

Every candidate gets a before/after record with:

- exact commit;
- platform/device/runtime;
- scene/workload fixture;
- peak process memory;
- renderer allocation counters;
- transient bytes;
- frame p50/p95;
- CPU time in target module;
- GPU upload bytes where relevant;
- input latency where relevant;
- copy count where measurable;
- correctness/visual regression result.

No performance claim is accepted without a reproducible fixture.

## Cross-platform conformance

### Android / Quest / PICO

- verify OpenXR capability parity;
- verify 16 KB alignment/release packaging remains intact;
- keep JNI thin;
- avoid Java/Kotlin objects on per-frame native hot paths.

### iOS / visionOS

- keep ObjC++/Swift bridge as translation layer;
- renderer ownership stays native;
- Metal resources are never exposed directly to JS;
- immersive-space lifecycle remains platform-owned.

### Web/WASM

- contract concepts map to WASM-compatible POD data;
- avoid relying on native pointer width assumptions in JS;
- track heap growth explicitly;
- preserve current web renderer provenance/build-id behavior;
- do not block future WebGPU work by baking WebGL objects into the logical contract.

## Upstream conflict policy

Custom work should move toward extension-owned files so upstream syncs touch fewer shared implementation files.

When an upstream ViroCore release changes a file we patch:
1. classify whether our change belongs in a contract adapter;
2. move it out if practical;
3. preserve the upstream implementation;
4. re-run contract tests and benchmark smoke tests.

## Migration manifest

`docs/eskiu-migration-manifest.json` records:
- module;
- current implementation;
- target contract domain;
- risk;
- measurement requirement;
- Eskiu eligibility;
- evidence-gate state.

That manifest is intended to become CI-readable later.

## Completion criteria

Native architecture is ready for Eskiu when:
- no public feature requires a concrete `VRO*` class across the bridge;
- hot-path ownership is explicit;
- backend capability discovery exists;
- the C++ reference backend passes contract tests;
- Web/OpenXR/visionOS have conformance coverage;
- the ReactVision native MCP has supplied verified Eskiu integration details;
- one candidate module can be swapped without changing React/TS code.
