# Eskiu / ViroCore PR roadmap

This is the execution graph for the Eskiu-ready Viro program. It is intentionally split into small PRs so upstream Viro/ViroCore changes remain easy to merge and regressions are attributable.

Legend:
- **R** = `mikevocalz/viro` (React/TypeScript/native bridge)
- **C** = `mikevocalz/virocore` (renderer/native engine)
- **M** = may proceed in parallel once dependencies are green
- **P** = production renderer behavior changes
- **N** = no production behavior change

## Foundation

### PR 00R — public architecture + feature matrix — N
Repository: R
Status: current planning PR
Depends on: none

Deliverables:
- public/bridge/renderer ownership rules;
- full custom-feature matrix;
- public Eskiu v0.9.2 facts;
- production native-MCP gate;
- upstream sync rules.

Acceptance:
- no package/API changes;
- every current custom feature maps to at least one engine-contract domain.

### PR 00C — native architecture + Eskiu ABI scaffold — N
Repository: C
Status: current planning/scaffold PR
Depends on: none

Deliverables:
- language-neutral ABI header;
- toolchain lock;
- CMake helper;
- Eskiu/C++ round-trip probe;
- pinned CI compiler asset/checksum;
- migration manifest.

Acceptance:
- ABI probe green;
- existing renderer CI unchanged;
- no current renderer source modified.

### PR 01C — C++ reference contract runtime — N
Repository: C
Depends on: 00C

Deliverables:
- `VROEngineContract.cpp`;
- ABI/version query;
- backend kind = C++;
- capability bitset;
- structured status;
- debug stale-handle registry;
- zero production call sites initially.

Acceptance:
- unit tests for version/size/unknown-field behavior;
- C ABI callable from a tiny C harness;
- no STL crosses the ABI.

### PR 02R — internal engine-contract TypeScript facade — N
Repository: R
Depends on: 00R, 01C

Deliverables:
- internal typed contract/capability model;
- native-module fallback;
- version negotiation;
- structured diagnostics;
- no public component migration.

Acceptance:
- unit tests with absent/old/new backend;
- no JSX breakage.

### PR 03C — measurement substrate — N
Repository: C
Depends on: 01C
Parallel: 02R

Deliverables:
- allocation/copy counters;
- scoped timing counters;
- optional per-module counters;
- benchmark snapshot schema;
- zero-cost/off-by-default release behavior.

Acceptance:
- counters themselves have bounded overhead;
- deterministic fixture output.

## Input and frame data

### PR 10C — bounded input sample ABI — N
Repository: C
Depends on: 01C, 03C

Deliverables:
- POD controller/hand/gaze/stylus sample structures;
- timestamp/frame id;
- bounded ring or latest-value ingestion;
- explicit producer/consumer thread rules.

Acceptance:
- saturation behavior tested;
- no heap allocation per sample after initialization.

### PR 11C — C++ input adapter — N
Repository: C
Depends on: 10C

Targets:
- `VROInputControllerBase`;
- OpenXR input;
- visionOS stylus ingress.

Acceptance:
- old and contract sample streams compare equal in fixtures;
- precision ink tests remain green.

### PR 12R — React input/capability adapter — N
Repository: R
Depends on: 02R, 11C

Deliverables:
- existing controller/stylus/hand/gaze public events use the internal contract facade;
- fallback behavior unchanged.

Acceptance:
- no public prop/event rename;
- event-shape snapshots unchanged.

### PR 13C — first Eskiu pilot: input sample storage — P, feature-flagged
Repository: C
Depends on: 10C, 11C, 03C, per-module native-MCP audit

Why first:
- isolated;
- bounded;
- allocation-sensitive;
- easy to compare bit-for-bit;
- low GPU coupling.

Acceptance:
- C++ and Eskiu results identical;
- memory improvement measured;
- no latency regression beyond agreed threshold;
- disabled by default until device matrix is green.

## Spatial / co-location / world understanding

### PR 20C — spatial frame ABI — N
Repository: C
Depends on: 01C

Deliverables:
- pose/anchor/shared-frame POD structures;
- frame identity/version;
- transform conventions documented;
- no network protocol in engine ABI.

### PR 21R — co-location/shared-frame split — N
Repository: R
Depends on: 02R, 20C

Keep in TS:
- room discovery;
- replication policy;
- Zustand stores;
- user/session orchestration.

Move behind contract:
- native poses;
- shared coordinate transforms;
- anchor samples.

Acceptance:
- network packet schema unchanged unless separately versioned.

### PR 22C — world-mesh chunk ABI + instrumentation — N
Repository: C
Depends on: 03C, 20C

Deliverables:
- chunk id/version;
- vertex/index borrowed views;
- update/remove semantics;
- peak-memory/update-time counters.

Acceptance:
- no copy unless ownership requires it;
- chunk lifecycle tests.

### PR 23C — Eskiu world-mesh processing experiment — P, feature-flagged
Repository: C
Depends on: 22C, native-MCP audit, benchmark baseline

Acceptance:
- same mesh topology/results;
- lower measured peak/transient memory;
- Quest/PICO/mobile regression fixtures green.

## Render surfaces / Rive / Three / WebGPU

### PR 30C — generic surface exchange ABI — N
Repository: C
Depends on: 01C, 03C

Deliverables:
- opaque surface handle;
- width/height/format;
- acquire/present/release lifecycle;
- synchronization token abstraction;
- backend capability flags.

Do not expose:
- OpenGL texture ids as the universal contract;
- Metal objects;
- WebGPU object layouts.

### PR 31R — ViroGpuPanel on generic surface contract — N
Repository: R
Depends on: 30C, 02R

### PR 32R — ViroRivePanel on generic surface contract — N
Repository: R
Depends on: 30C, 31R
Parallel with: 33R

### PR 33R — ViroThreeJSPanel on generic surface contract — N
Repository: R
Depends on: 30C, 31R
Parallel with: 32R

Acceptance for 31-33:
- current JSX remains unchanged;
- surface lifecycle leak tests;
- visual regression on supported platforms;
- Web path remains first-class.

### PR 34C — Eskiu surface bookkeeping experiment — P, feature-flagged
Repository: C
Depends on: 30C, native-MCP audit, benchmark baseline

Scope:
- bookkeeping/resource tables only first;
- not shader/GPU driver replacement.

## Camera, media and recording

### PR 40C — camera/media buffer ABI — N
Repository: C
Depends on: 01C, 03C, 30C

Deliverables:
- plane/view metadata;
- borrowed frame lifetime;
- format/color-space metadata;
- zero/one-copy accounting;
- release callback/handle semantics.

### PR 41R — ViroCameraTexture/media bridge adapter — N
Repository: R
Depends on: 40C, 02R

### PR 42C — recording/video surface adapter — N
Repository: C
Depends on: 40C

### PR 43C — Eskiu frame-buffer lifecycle experiment — P, feature-flagged
Repository: C
Depends on: 40C, native-MCP audit, baseline

Acceptance:
- copy count equal or lower;
- no dropped/reordered frame regression;
- recording fixtures green.

## Geometry / Mapbox / model loading

### PR 50C — dynamic geometry stream ABI — N
Repository: C
Depends on: 01C, 03C

Deliverables:
- immutable descriptor + update ranges;
- vertex/index views;
- mesh versioning;
- GPU upload accounting.

### PR 51R — ViroPolyline / Mapbox route adapter — N
Repository: R
Depends on: 50C, 02R

Rules:
- geospatial route logic remains outside renderer;
- renderer receives geometry, not Mapbox policy/API objects.

### PR 52C — glTF/GLB loader memory instrumentation — N
Repository: C
Depends on: 03C

Targets:
- `VROGLTFLoader`;
- image decode buffers;
- parsed model lifetime;
- texture handoff;
- cancellation.

### PR 53C — Eskiu dynamic-geometry buffer experiment — P, feature-flagged
Repository: C
Depends on: 50C, native-MCP audit, baseline

### PR 54C — Eskiu asset transient-buffer experiment — P, feature-flagged
Repository: C
Depends on: 52C, native-MCP audit, baseline

Do not rewrite the full loader first. Start with one isolated transient-buffer stage.

## Platform backends

### PR 60C — OpenXR contract adapter — N
Repository: C
Depends on: 01C, 10C, 20C

Targets:
- Quest;
- PICO;
- future OpenXR glasses.

Rules:
- device identity is capability data;
- no Meta/PICO enum leaks into generic engine ABI unless unavoidable.

### PR 61R — XR runtime facade on contract — N
Repository: R
Depends on: 60C, 02R

Preserve:
- current Meta/PICO/Quest/glasses helpers for compatibility;
- gradually implement them from generic capabilities.

### PR 62C — visionOS/Metal contract adapter — N
Repository: C
Depends on: 20C, 30C, 40C

### PR 63R — visionOS/stylus facade migration — N
Repository: R
Depends on: 62C, 12R

### PR 64C — Web/WASM contract parity — N
Repository: C
Depends on: 01C, 20C, 30C, 50C

Requirements:
- pointer-width-safe JS/WASM boundary;
- WASM heap metrics;
- preserve build provenance;
- no WebGL-specific universal ABI.

## Heavy subsystems — profile before language migration

### PR 70C — physics transfer audit — N
Repository: C
Depends on: 03C

No Eskiu rewrite is approved unless CPU/memory profiling shows the boundary is worth it.

### PR 71C — particles/update audit — N
Repository: C
Depends on: 03C

### PR 72C — post-process scratch-memory audit — N
Repository: C
Depends on: 03C

Potential outcomes for 70-72:
- keep C++;
- only change ownership/allocation;
- isolate into a contract;
- approve a measured Eskiu experiment.

## Benchmark and rollout

### PR 80C — cross-platform benchmark harness — N
Repository: C
Depends on: 03C

Fixtures:
- idle scene;
- controller/stylus stress;
- co-location pose stress;
- large world mesh;
- dynamic route/polyline;
- Rive/GPU panel;
- camera texture;
- large GLB;
- particles/physics where applicable.

### PR 81R — app-side conformance harness — N
Repository: R
Depends on: 02R, selected adapters

Targets:
- Android phone/tablet;
- iPhone/iPad;
- Quest;
- PICO;
- visionOS;
- Web;
- glasses target when available.

### PR 82C — backend A/B switch + snapshot format — N
Repository: C
Depends on: at least one Eskiu pilot, 80C

Allows the same fixture to run C++ and Eskiu implementations and emit directly comparable metrics.

### PR 83C — first production Eskiu backend promotion — P
Repository: C
Depends on:
- successful pilot;
- native-MCP audit;
- 80C;
- 82C;
- device matrix;
- soak period.

Promotion criteria:
- correctness parity;
- meaningful memory win;
- no unacceptable frame/input regression;
- rollback switch retained.

## Parallelization graph

After 00C/01C/03C:
- input (10C);
- spatial (20C);
- surface (30C);
- loader instrumentation (52C);
- heavy-subsystem audits (70C-72C)
can proceed in parallel.

After 02R:
- React input;
- co-location;
- panel;
- media;
- Mapbox;
- platform facade
work can proceed as their native contracts land.

Eskiu implementation PRs are intentionally later than contract/instrumentation PRs. This prevents a language migration from hiding an ownership bug or changing public behavior at the same time.

## Merge discipline

Every implementation PR must state:
- public API impact;
- ABI impact;
- touched platforms;
- ownership changes;
- thread changes;
- allocation/copy changes;
- benchmark fixture;
- rollback mechanism.

Never combine in one PR:
- new contract + production backend swap;
- public API redesign + Eskiu migration;
- upstream ReactVision sync + custom backend migration.

That separation is what keeps this fork maintainable.
