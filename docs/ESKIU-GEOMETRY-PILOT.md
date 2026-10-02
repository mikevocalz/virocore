# First Eskiu engine pilot: geometry validation

This is the first extension-owned implementation of an existing Viro engine contract in
Eskiu.

It was chosen before OpenXR/input/world-mesh processing because it has:
- no GPU resources;
- no platform runtime objects;
- no allocation across the language boundary;
- no callbacks;
- no renderer-thread requirement;
- deterministic status outputs.

## Boundary

The exported Eskiu functions take pointers to the exact v0.1 C ABI descriptors:

- `viro_eskiu_geometry_validate(VROEngineGeometryDesc*)`
- `viro_eskiu_geometry_range_validate(VROEngineGeometryDesc*, VROEngineGeometryRangeUpdate*)`

No aggregate is passed by value across C++/Eskiu.

## Differential gate

The test constructs valid and invalid descriptors and sends each case to both:
1. the shipping C++ reference implementation;
2. the experimental Eskiu implementation.

Any status-code mismatch fails CI.

## Promotion

This PR does **not** switch production ViroCore to Eskiu. Promotion requires:
1. differential correctness green;
2. the common backend benchmark harness;
3. platform-target proof for the deployment target;
4. native-MCP audit for the production call site;
5. feature-flagged A/B integration.

This file lives under `experimental/eskiu` until those gates are met.
