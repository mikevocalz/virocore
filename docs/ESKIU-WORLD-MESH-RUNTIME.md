# Eskiu world-mesh runtime backend

The world-mesh Eskiu validator already had differential parity with the C++
reference path. This track adds domain-scoped runtime selection without changing
the production default.

## Runtime selection

`VROEngineWorldMeshBackend` resolves the `worldMesh` domain through
`VROEngineBackendSelector` and dispatches chunk validation to C++ or Eskiu.

The Eskiu world-mesh object continues to reuse the Eskiu geometry validator, so
the native link contains both objects. No C++ renderer class layout crosses the
language boundary.

## Metrics

The existing world-mesh payload metrics remain the reference instrumentation and
are intentionally unchanged by this PR. Backend selection changes validation,
not measurement semantics.

## Rollout

- explicit C++: supported;
- explicit Eskiu: supported when the Eskiu objects are linked;
- AUTO: remains C++;
- production AUTO promotion: still gated on target-device update-time, memory,
  allocation, and frame-time evidence plus soak/rollback.
