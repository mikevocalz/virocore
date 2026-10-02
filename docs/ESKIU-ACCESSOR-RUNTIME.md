# Eskiu accessor runtime backend

The accessor materialization contract is the first asset-loading path where the
Eskiu implementation performs the complete operation rather than only
validation. It handles:

- tightly packed output allocation supplied by the caller;
- strided base accessor reads;
- zero-base sparse-only accessors;
- uint8 / uint16 / uint32 sparse indices;
- sparse value patching.

This track makes that implementation selectable through the
`assetLoading` backend domain.

## glTF integration

`VROGLTFLoader::materializeAccessorData` now describes both the base accessor
and sparse patch in one `VROEngineAccessorMaterializeDesc` and calls
`viro_engine_accessor_materialize_selected` once.

That removes the second C++ sparse-patch loop from the production path and makes
the entire dense materialization operation an interchangeable C++/Eskiu backend.

`AUTO` stays C++ until same-device asset-load time, peak memory, transient
allocation, copy count, correctness, and soak gates pass.


## Production target wiring

- **Android arm64-v8a / x86_64:** pass `-DVIRO_ENABLE_ESKIU_ACCESSOR=ON` to the native CMake build. The build invokes pinned-compatible `eskiuc`, emits a PIC object for the Android target triple, links it into `viro_renderer`, and defines the domain availability macro.
- **C++ fallback:** remains the default when the option is off, preserving rollback and builds that do not install Eskiu.
- **Web/WASM:** the selected C ABI facade is compiled into the WASM target, but Eskiu 0.9.2 does not document a WebAssembly code-generation backend. WASM therefore advertises C++ only instead of pretending an Eskiu backend exists.
- **Apple:** native Eskiu object emission for iOS and visionOS is covered by the repository Apple target probe. Product-link promotion remains gated separately from portable backend correctness.
