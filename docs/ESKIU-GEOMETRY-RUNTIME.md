# Eskiu geometry runtime backend

The existing Eskiu geometry implementation already had C++/Eskiu differential
parity for the frozen v0.1 geometry and range-update contracts. This track adds
the missing runtime selection seam.

## Runtime contract

`VROEngineGeometryBackend` resolves the `geometry` domain through the shared
engine backend selector and dispatches validation to either:

- the current C++ reference implementation; or
- `experimental/eskiu/VROEngineGeometryEskiu.esk`.

The public geometry ABI is unchanged. `AUTO` remains C++ until the per-domain
promotion gates have real target measurements.

## Validation

The runtime conformance test verifies:

- Eskiu is reported available when its object is linked;
- `AUTO` resolves to C++;
- explicit C++ resolves to C++;
- explicit Eskiu resolves to Eskiu;
- valid and invalid geometry descriptors retain status parity;
- valid and invalid range updates retain status parity.

This makes geometry runtime-selectable without yet making Eskiu the release
default.
