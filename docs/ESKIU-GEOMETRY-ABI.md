# Engine geometry stream ABI

This contract is the renderer-facing seam for dynamic geometry.

Initial consumers:
- ViroPolyline;
- Mapbox AR route projection;
- dynamic meshes;
- later world-mesh chunk upload.

## Policy boundary

Geospatial and Mapbox logic stays outside ViroCore. The renderer receives positions,
attributes, indices and update ranges—not route objects, tiles, directions APIs or map state.

## Ownership

All vertex/index/update buffers are borrowed views:
- valid only for the documented call scope in future adapters;
- never retained without an explicit copy/owned-resource operation;
- no allocator crosses the ABI.

## Versioning

`geometry_id` identifies the logical mesh.
`version` changes after successful updates.
A range update names both `base_version` and `next_version` so stale/reordered writes
can be rejected deterministically.

## Measurement

Adapter PRs use the engine measurement substrate for:
- copied bytes;
- upload bytes;
- update count;
- CPU time.

The first Eskiu geometry experiment is not approved until the C++ adapter produces a
repeatable baseline.
