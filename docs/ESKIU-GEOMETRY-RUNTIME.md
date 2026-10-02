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
