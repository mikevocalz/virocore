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
