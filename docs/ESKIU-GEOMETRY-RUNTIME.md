# Eskiu geometry runtime backend

The existing Eskiu geometry implementation already had C++/Eskiu differential
parity for the frozen v0.1 geometry and range-update contracts. This track adds
runtime selection **and moves the real dynamic-geometry buffer-preparation hot
path behind it**.

## Runtime contract

`VROEngineGeometryBackend` resolves the `geometry` domain through the shared
engine backend selector and dispatches:

- geometry descriptor validation;
- range-update validation;
- padded buffer preparation.

The public geometry descriptor ABI is unchanged. `AUTO` remains C++ until the
per-domain promotion gates have real target measurements.

## Dynamic geometry allocation fix

Previously every padded attribute update in `VRODynamicGeometry` created a new
`std::vector<uint8_t>(totalBytes, 0)`, copied active bytes into it, then handed
that buffer to `VROData`, which copies again.

The dynamic geometry now owns one reusable `_paddingScratch` buffer. It only
grows when needed. The active-copy + tail-zero operation is performed through
`viro_engine_geometry_pad_copy_selected`, so an Eskiu-enabled renderer can
execute that preparation without changing the public Viro APIs or renderer class
layout.

This removes the per-attribute staging-vector allocation from steady-state
updates. `VROData`'s existing copy remains unchanged in this step.

## Validation

The runtime conformance test verifies:

- Eskiu is reported available when its object is linked;
- `AUTO` resolves to C++;
- explicit C++ resolves to C++;
- explicit Eskiu resolves to Eskiu;
- valid and invalid geometry descriptors retain status parity;
- valid and invalid range updates retain status parity;
- pad-copy preserves active bytes, zeroes the tail, and rejects undersized
  output on both selected backends.

Android and web renderer source lists include the selector facade because
`VRODynamicGeometry` now calls it. The Eskiu object itself remains opt-in until
the native build integration track lands.


## Production target wiring

- **Android arm64-v8a / x86_64:** pass `-DVIRO_ENABLE_ESKIU_GEOMETRY=ON` to the native CMake build. The build invokes pinned-compatible `eskiuc`, emits a PIC object for the Android target triple, links it into `viro_renderer`, and defines the domain availability macro.
- **C++ fallback:** remains the default when the option is off, preserving rollback and builds that do not install Eskiu.
- **Web/WASM:** the selected C ABI facade is compiled into the WASM target, but Eskiu 0.9.2 does not document a WebAssembly code-generation backend. WASM therefore advertises C++ only instead of pretending an Eskiu backend exists.
- **Apple:** native Eskiu object emission for iOS and visionOS is covered by the repository Apple target probe. Product-link promotion remains gated separately from portable backend correctness.
