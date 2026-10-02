# Eskiu migration status — Phase 2

The fork has completed the repository-side foundation needed for selective Eskiu backend work.

## Proven in CI / repository tests

- Eskiu v0.9.2 host C ABI linkage
- Apple iOS/visionOS object-target probes
- Android AArch64 object emission
- JNI shared-library linkage through Gradle/NDK
- AAR packaging with arm64-v8a and 16 KB alignment
- WebAssembly object emission and link
- language-neutral engine contracts for input, spatial frames, surfaces, geometry, world mesh and media
- renderer-neutral XR semantic conformance fixtures
- C++ vs Eskiu geometry differential parity
- Eskiu shadow implementation of the bounded input ring
- C++ baseline/A-B benchmark format
- generic and named subsystem metrics
- profiling gates for physics, particles, post-processing, glTF, world mesh and media paths

## What this does not claim

No production renderer subsystem has been blindly replaced. C++ remains the reference backend.

A production C++ → Eskiu swap still requires, for that exact module:
1. ReactVision native/platform MCP mapping and current ownership/threading conventions;
2. a device-specific baseline fixture;
3. C++ vs Eskiu correctness parity;
4. measured memory/frame-time benefit;
5. feature-flagged soak coverage on the affected targets.

This is intentional: Eskiu is now a validated backend option in the fork, not a mandatory rewrite.
