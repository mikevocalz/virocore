# Bounded engine input ABI

This contract is storage-only. It does not yet replace Viro's current controller,
OpenXR, hand, gaze, or visionOS stylus paths.

## Threading

A ring is **single producer / single consumer**:

- exactly one producer thread calls `push`;
- exactly one consumer thread calls `pop`;
- read-only counters may be queried from diagnostic threads;
- switching producer or consumer threads requires external synchronization.

The implementation is lock-free for push/pop and allocates only during `create`.

## Saturation

The v0.1 policy is **drop-new**:

- existing queued transitions are never overwritten;
- a saturated push returns `VRO_ENGINE_INPUT_FULL`;
- `dropped_count` increments;
- the producer may coalesce/latest-value samples above this layer if that policy is
  appropriate for a particular source.

This is deliberate for the first contract because button/transition ordering must not be
silently overwritten by the storage primitive.

## Payloads

One 96-byte POD sample can carry:

- controller pose/buttons/axes;
- one hand-joint pose;
- gaze origin/direction;
- stylus pose/pressure/tilt/buttons.

The fixed union makes the ring allocation deterministic and keeps the C/Eskiu boundary free
of variant heap allocations.

## Next adapter PR

The next input PR compares existing Viro controller/stylus samples against this contract
in fixtures before any production routing changes. No Eskiu implementation is approved
until those old/new C++ streams match.
