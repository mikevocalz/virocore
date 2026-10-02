# SPECS backend semantic parity

SPECS is a Lens Studio renderer backend, not a ViroCore port. Every backend must preserve the same public Viro semantics.

- Positions, polyline points, hit-test positions/distances are **meters**.
- Viro JSX rotations are Euler **degrees**; the reference helper converts to radians and matches `VROQuaternion::set(x,y,z)` composition exactly.
- `ViroPolyline.thickness` is the full diameter in meters; radius-based backends use `thickness / 2`.
- Lens centimeters are private to the Lens backend boundary and must never escape into application callbacks.
- This header is a conformance fixture, not a renderer dispatch ABI, and adds no Lens dependency to ViroCore.

The focused C++ test compares the portable quaternion formula directly against ViroCore's implementation so alternate renderers cannot silently choose a different Euler order.
