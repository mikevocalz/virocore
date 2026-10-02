# Eskiu spatial runtime backend

This closes the remaining selector gap for the language-neutral spatial/shared
frame contract.

The Eskiu backend implements:

- rigid-transform identity;
- quaternion-normalized composition;
- rigid-transform inversion;
- point transforms;
- shared-frame validation.

The implementation stays below platform co-location APIs. Quest/PICO OpenXR
anchors, Apple spatial anchors, and ReactVision cloud anchors keep their existing
platform ownership; only the portable transform/shared-frame math is
interchangeable.

`AUTO` remains C++ until target parity, shared-frame drift, frame-time, and soak
evidence justify promotion.


## Production target wiring

- **Android arm64-v8a / x86_64:** pass `-DVIRO_ENABLE_ESKIU_SPATIAL=ON` to the native CMake build. The build invokes pinned-compatible `eskiuc`, emits a PIC object for the Android target triple, links it into `viro_renderer`, and defines the domain availability macro.
- **C++ fallback:** remains the default when the option is off, preserving rollback and builds that do not install Eskiu.
- **Web/WASM:** the selected C ABI facade is compiled into the WASM target, but Eskiu 0.9.2 does not document a WebAssembly code-generation backend. WASM therefore advertises C++ only instead of pretending an Eskiu backend exists.
- **Apple:** native Eskiu object emission for iOS and visionOS is covered by the repository Apple target probe. Product-link promotion remains gated separately from portable backend correctness.
