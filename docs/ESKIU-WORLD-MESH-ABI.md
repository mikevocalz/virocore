# Eskiu world-mesh boundary

This contract keeps world-mesh transport independent from C++ class layout and reuses
the generic geometry ABI rather than creating a second vertex/index representation.

## Ownership

All vertex/index/confidence views are borrowed. A consumer may not retain the pointers
after the producer's documented call scope unless a future lease/resource API explicitly
extends that lifetime.

## Update model

- `UPSERT`: a complete triangle chunk with a monotonically increasing version.
- `REMOVE`: retires a chunk id/version and carries no geometry payload.

Chunk ids let ARKit/ARCore/OpenXR implementations update only the changed region rather
than rebuilding one giant mesh at the language boundary.

## Metrics

The v0.1 counters track:
- number of upserts/removes;
- cumulative vertex/index/confidence payload bytes;
- high-water payload size for one chunk;
- last timestamp.

They deliberately do **not** claim process RSS or transient allocator peak. Those remain
benchmark-harness measurements. This distinction prevents an ABI counter from being
misreported as an end-to-end memory number.

## Eskiu migration rule

An Eskiu implementation may own chunk bookkeeping/processing behind this ABI after the
native-MCP audit and baseline benchmark. The public Viro world-mesh API and the existing
`VROARWorldMesh` C++ implementation remain unchanged until A/B parity is proven.
