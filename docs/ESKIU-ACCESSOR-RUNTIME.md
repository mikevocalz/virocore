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
