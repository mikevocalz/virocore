# Engine spatial/shared-frame ABI

This ABI is the native math/data seam for:
- co-location coordinate frames;
- cloud/local anchor adapters;
- shared-space alignment;
- future room/world mesh frame ownership;
- lightweight glasses/headset runtime frame transforms.

It deliberately does **not** own:
- networking;
- room codes;
- participant identity;
- React/Zustand state;
- cloud-anchor API policy.

Those remain in the higher ViroReact/application layers.

## Transform convention

`A_from_B` transforms a point expressed in B coordinates into A coordinates.

Composition:

```text
A_from_C = A_from_B * B_from_C
```

`viro_engine_transform_point(A_from_B, point_B)` returns `point_A`.

## ABI rules

- structs begin with `struct_size`;
- v0.1 prefixes are frozen at 36 and 80 bytes;
- minor versions append only;
- cross-language use is pointer-based;
- no allocation occurs in math/validation functions;
- frame IDs/revisions are values, not native object handles.

## Next adapters

Separate PRs map:
1. existing Viro co-location transforms;
2. visionOS shared-space alignment;
3. OpenXR/PICO shared reference spaces;
4. world-mesh chunks

onto this contract without changing application replication behavior.
