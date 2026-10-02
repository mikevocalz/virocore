# XR backend conformance contract

This repository remains the native ViroCore implementation. SPECS/Lens Studio
is **not** linked into ViroCore and is not represented as a C++ renderer backend.

Instead, every renderer is expected to conform to the same observable Viro
semantics.

The machine-readable fixture lives at:

`tests/xr_backend/conformance.json`

and is checked by:

`tests/xr_backend/validate_conformance.py`

## Canonical semantics

- right-handed coordinates
- +X right
- +Y up
- -Z forward
- public Viro positions/distances/line widths are meters
- public Viro JSX rotations are Euler degrees
- portable backend rotations are quaternion XYZW
- interaction source and interaction phase are separate
- world-query results return positions/distances in meters
- polyline width is a full width; a tube backend derives radius as width / 2

A renderer may use different native units. Lens Studio, for example, uses
centimeters internally. Conversion belongs at the backend boundary.

## Backend comparison

A ViroCore implementation and a Lens Studio implementation can use completely
different rendering engines while still conforming:

```text
Viro public semantics
        |
   conformance fixture
    /           \
ViroCore      Lens Studio
C++ renderer  SPECS renderer
```

The fixture is intentionally renderer-independent. Do not add Lens Studio types,
OpenXR handles, Metal objects, Android classes, or JS callbacks to it.

## What belongs here

- coordinate/unit conventions
- normalized interaction vocabulary
- hit-result semantics
- capability vocabulary
- line/geometry interpretation
- fixed cross-backend examples

## What does not

- product/device detection
- Lens Studio package APIs
- React components
- renderer allocation/lifetime
- native graphics handles
- networking or co-location provider policy
