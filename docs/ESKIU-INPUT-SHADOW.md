# C++ engine input shadow adapter

This adapter translates existing Viro math/input values into the bounded input ABI.

It is intentionally **not wired into production event dispatch yet**. That separation keeps
the current event path authoritative while we establish mapping parity and benchmarking.

## Mapping guarantees

- source id is preserved;
- timestamp and frame id are supplied by the producer;
- sequence is monotonic within one shadow instance;
- position and quaternion components are copied without coordinate conversion;
- controller axes/buttons/trigger/grip remain explicit;
- stylus pressure/tilt/buttons remain explicit;
- gaze direction remains a vector, not a derived orientation;
- each hand joint is one bounded sample.

## Why this is the comparison seam

The later production adapter can mirror real `VROInputControllerBase` / OpenXR samples into
this class under a feature flag. Tests and diagnostics can then compare the legacy event
source against the contract samples before any backend is changed.

An Eskiu implementation of the storage layer is not promoted until it produces the same
sample sequence and improves measured memory/allocation behavior.
