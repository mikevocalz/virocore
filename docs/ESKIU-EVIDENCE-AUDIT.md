# Eskiu / Viro evidence audit

Audit date: 2026-10-01 America/New_York (2026-10-02 UTC)

This file records the evidence used to start the Eskiu-ready Viro/ViroCore architecture program. It is intentionally separate from the architecture document so future upstream syncs can re-run and update the evidence without rewriting the design.

## Public Eskiu source verified

Repository:
- `doranteseduardo/eskiu`

Release used for the initial integration:
- tag: `v0.9.2`
- tag object: `d7ad34db0130acb2e403b443b1257032103220fc`
- release commit: `95d7ece7ea8d72bc106a141bc031a1648ed8f2ac`
- published: 2026-09-30
- compiler: `eskiuc`
- LLVM requirement: 21+
- release binaries: Linux x86-64, Linux arm64, macOS arm64, Windows x86-64

The pinned release asset hashes are stored in `tools/eskiu/toolchain.json`.

## Public ABI facts verified from Eskiu v0.9.2

From `docs/dev/abi.md` and the compiler documentation:

- Eskiu targets the platform's native C ABI through LLVM.
- There is no separate Eskiu calling convention.
- `extern` uses exact C symbol names.
- Non-template top-level Eskiu functions retain their source symbol names.
- Eskiu can emit a native object using `-c` / an `.o` output.
- Plain structs use target-natural C layout.
- packed and `#pragma pack(N)` layouts are specified.
- pointers lower to native pointers.
- aggregate by-value C ABI lowering is documented for AArch64, x86-64 SysV, Windows x64, ARM32/AAPCS and 32-bit x86.
- C callbacks are supported; aggregate signatures may use generated `__cabi_*` thunks.
- cross compilation accepts `--target`, `--mcpu`, `--mattr`, and `--reloc`.
- `--freestanding` exists for runtime-free targets.

## Aggregate entry-point caveat verified by CI

A direct C++ call to an ordinary top-level Eskiu symbol using a seven-float struct
by value did **not** preserve values in our Linux x86-64 probe. This is consistent
with the distinction in Eskiu's ABI documentation:

- Eskiu -> `extern` C calls receive target C-ABI aggregate lowering;
- C callbacks into Eskiu can use generated `__cabi_*` thunks for aggregate signatures;
- ordinary Eskiu functions may use Eskiu's internal aggregate lowering between Eskiu callers.

Therefore this fork does not treat "source symbol is visible" as proof that an arbitrary
aggregate-by-value top-level Eskiu function is a safe C entry point. Cross-language Viro
contracts use scalars, opaque handles, and pointer/view structures unless a C-ABI thunk
path has its own conformance test.

The existing passing ABI probe on `main` already follows this rule: scalar calls cross
directly, while the pose-like POD crosses by pointer.

## ReactVision Viro case-study facts

The Eskiu repository's `site/case-study-reactvision.html` states that ReactVision selectively replaces memory-sensitive C++ ViroCore components with Eskiu modules linked over the C ABI, preserving the surrounding engine. It reports roughly 85% less memory in the rendering modules migrated so far.

The case study specifically describes memory-sensitive candidates including image/texture preprocessing, scene graph and physics, but that does **not** establish that every one of those systems is already migrated in the public ViroCore tree.

## Viro / ViroCore branch audit

### mikevocalz/viro

Branches present during audit included:

- `main`
- `decax9-three-panel`
- `develop`
- `upgrade/viro-3.0.1`
- `sync/reactvision-v3.0.2`
- `integrate/reactvision-v3.0.2`
- `feat/xr-platform-foundation`
- `feat/cross-platform-spatial-layout`
- `feat/meta-vr-glasses-target`
- `feat/meta-vr-glasses-capabilities`
- `fix/pico-web-meta-spatial-convergence`
- `codex/rive-viro-panel`
- `codex/visionos-spatial-stylus`
- `codex/precision-renderer-package`
- `codex/pico-cli-bridge`
- other historical branches

No obvious Eskiu source/build tree was found in the audited Viro branch heads. The custom work remains primarily TypeScript/native-bridge/platform integration.

### mikevocalz/virocore

Relevant native branches inspected included:

- `main`
- `develop`
- `feat-web-platform`
- `v2.57.3-xr`
- `v2.55.0-nitro-canvas`
- `pico-support`
- `metahorizon-support`
- `visionos-support`
- `codex/rive-viro-panel`
- `upgrade/virocore-3.0.1`

No obvious Eskiu source/build tree was found in those branch trees. This does not prove ReactVision has no private/in-progress Eskiu integration.

### upstream ReactVision/viro

Audited:
- `main`
- `develop`
- `release/3.0.3`
- `feature/function-region-pinning`

No Eskiu source tree was exposed in these public branch trees during the audit.

### upstream ReactVision/virocore

Audited:
- `main`
- `develop`
- `release/3.0.3`
- `feat/function-region-pinning`
- current chore/investigation branches visible at audit time

The recursive Git trees inspected were not truncated and did not expose an obvious Eskiu source/build tree.

## Viro MCP finding

ReactVision documents an official ViroReact MCP intended to expose current ViroReact knowledge.

The documented Viro MCP tool `reactviro_find_native_bridge` points native implementation research to a separate ReactVision platform/native MCP containing ViroCore C++, JNI/iOS bridge and SLAM knowledge.

Important consequence:

**A public GitHub scan is not enough to conclude that ReactVision has no internal Eskiu implementation or conventions.**

The public Eskiu repository is sufficient to establish the language/toolchain/C-ABI integration. The ReactVision native/platform MCP is still required before replacing a production ViroCore module, because it may contain current implementation details that are not exposed in the public Viro/ViroCore branches.

## What is proven in this fork

The `Eskiu ABI probe` workflow proves against pinned v0.9.2:

1. the release binary downloads and matches the pinned checksum;
2. `eskiuc` runs;
3. an Eskiu source compiles to an object;
4. C++ links and calls Eskiu;
5. Eskiu calls an `extern "C"` C++ symbol;
6. a pose-like POD struct crosses by pointer and preserves layout/value semantics.

This establishes the technical viability of the extension seam independently of ReactVision's unpublished production wiring.

## Items to re-audit before each production module migration

- current Eskiu release and ABI changelog;
- current ReactVision Viro/ViroCore release/develop branches;
- Viro MCP public API mapping;
- `reactviro_find_native_bridge` result;
- ReactVision platform/native MCP result;
- target module ownership/threading;
- target platform compilation flags;
- baseline performance fixture.

## Items that are deliberately *not* assumed

- that ReactVision's private Eskiu modules use the same file organization we will choose;
- that every memory-sensitive subsystem should move to Eskiu;
- that an 85% module-level memory result transfers to our workload;
- that a C++ implementation should be removed after an Eskiu experiment succeeds on only one platform;
- that Web/WASM, OpenXR and Metal should share backend-native object layouts.

Those decisions remain measurement- and capability-driven.
