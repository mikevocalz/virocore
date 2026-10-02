# Eskiu surface + media runtime backends

The surface and media Eskiu implementations were already differential-tested
against the C++ references. This track makes those implementations selectable at
runtime without changing any Viro React component API.

## Surface

The surface domain now resolves through `VROEngineBackendSelector` for:

- surface descriptor validation;
- surface-frame validation;
- monotonic frame ordering.

## Media

The media domain now resolves through the same selector for:

- CPU media-plane validation;
- media-frame validation;
- payload-byte accounting.

Existing media metrics remain the reference instrumentation so measurement
semantics do not change while backend execution is being compared.

## Promotion policy

Explicit Eskiu is enabled only in binaries that link the corresponding Eskiu
objects. `AUTO` remains C++ until same-device copy count, payload memory,
camera/video latency, frame-time, and soak gates pass.
