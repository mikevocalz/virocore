# ADR-0001: Tracking origin — LOCAL vs LOCAL_FLOOR vs STAGE

Status: proposed — pending on-device verification on PICO Neo3, PICO 4, and PICO 4 Ultra.

## Context

Upstream `VROSceneRendererOpenXR::createReferenceSpace()` deliberately uses
`XR_REFERENCE_SPACE_TYPE_LOCAL`. The code comment explains why:

> Use LOCAL space so the Viro world origin matches the eye level at session start.
> STAGE space places Y=0 at the floor (~1.6 m below the eye), which causes objects
> placed at Viro world (0,0,-2) to appear ~39° below the horizon — near or past
> the bottom of the Quest 3's physical FOV. LOCAL space matches every other Viro
> platform's convention (camera at scene origin, looking forward).
> Floor-relative placement (STAGE) can be addressed in a dedicated M-series milestone.

That default is correct for panels and content composed at eye level, but apps
that place content on the ground — a ViroQuad at y=0 that should coincide with
the physical floor — cannot express that in LOCAL space without guessing the
user's height. This ADR is that "dedicated milestone" for the PICO fork.

## Decision

Keep the default unchanged and add an opt-in floor mode.

- **`Eye` (default).** `XR_REFERENCE_SPACE_TYPE_LOCAL`, identity pose — exactly
  upstream's current behavior. No existing app changes appearance.
- **`Floor` (opt-in).** Selected via the JS-side `trackingOrigin: "floor"` option
  (see `handoff.md`). Resolved through a fallback ladder at session creation:
  1. `XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR` when the runtime enumerates it
     (OpenXR 1.1 core, or the `XR_EXT_local_floor` extension — PICO exposes the
     extension on PICO 4 Ultra only, per the extension matrix at
     developer.picoxr.com).
  2. **Emulation.** Locate STAGE relative to LOCAL once at session start, take
     the Y offset, and create a LOCAL space whose `poseInReferenceSpace` shifts
     the origin down to floor level. This is the emulation the
     `XR_EXT_local_floor` spec text itself describes for runtimes without the
     extension. Requires STAGE to be enumerated and locatable.
  3. Neither available → remain in `Eye` mode and report the actual mode to the
     app layer so it can adapt. Never substitute a hardcoded human-height
     constant; a wrong guess puts the floor plane through the user's shins or
     leaves it hovering.

## Consequences

- **App-driven recenter.** `recenterTracking()` currently hardcodes
  `XR_REFERENCE_SPACE_TYPE_LOCAL` and zeroes Y when it rebuilds `_stageSpace`.
  In Floor mode the rebuild must use the same space type (or the same emulated
  offset) the ladder originally selected, so recentering preserves floor-at-y=0
  instead of silently reverting to eye level.
- **System recenter.** The `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING`
  handler today only logs ("content may shift"). With Floor mode it must
  recreate the reference space and, in the emulation path, re-derive the
  STAGE-to-LOCAL Y offset — the runtime may have moved either space.
- **Co-location anchors.** Upstream PR #370's anchors resolve poses against the
  app reference space (`_stageSpace`), the same handle the ladder replaces, so
  anchored content follows the selected mode with no anchor-side changes.
- **Reporting.** The resolved mode (requested vs actual) must be surfaced to JS
  so an app that asked for `floor` and got `eye` can reposition content instead
  of rendering a floor plane at eye height.

## Open questions

- OPEN QUESTION: whether PICO's runtime enumerates STAGE (and returns a valid
  location for it) on Neo3 and PICO 4, which the emulation rung depends on.
- OPEN QUESTION: whether PICO fires `REFERENCE_SPACE_CHANGE_PENDING` on the
  system-level recenter (long-press Home), and with what `changeTime` semantics.
- Height of the LOCAL origin above the physical floor on each device:
  MEASURE ON DEVICE (needed to validate rung 2's derived offset).
