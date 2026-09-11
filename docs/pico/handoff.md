# Handoff spec — trackingOrigin, hands toast, recenter affordance

Three surfaces introduced with the reference-space work. Copy strings live in
`ux-copy.md`; the space-selection logic lives in `adr-0001-reference-space.md`.
Distance values reference the `comfortableUI` token (1.5 m) from
`viroreact-spatial-layout-system`, as the comfort skill directs.

## (a) `trackingOrigin: "eye" | "floor"` — XR navigator prop

JS prop on the XR scene navigator. Default `"eye"` — existing apps are
unaffected.

- **States:**
  - `requested`: the prop value (`"eye"` or `"floor"`).
  - `resolved`: what the native ladder actually produced — `"eye"`,
    `"floor-native"` (LOCAL_FLOOR), or `"floor-emulated"` (STAGE-derived
    offset). Exposed to JS via a callback/event so the app can adapt when
    `"floor"` degrades to `"eye"`.
- **Entry:** Read once at session creation; the ladder in ADR-0001 runs inside
  `createReferenceSpace()`.
- **Exit / change:** OPEN QUESTION — whether changing the prop mid-session
  rebuilds the space live or requires navigator remount. Spec the conservative
  behavior (remount) until the live path is verified on device.
- **Animation constraint:** None — this is configuration, not UI. Any content
  shift caused by a mode change happens behind a fade (≤200 ms), never as
  visible world translation.
- **Placement note:** Not applicable (no visual surface).

## (b) "Controllers not detected — using hands" status toast

- **States:** `hidden` → `visible` → `hidden`. One-shot per continuous
  hands-only period; re-arms only after controllers return.
- **Entry:** Native input controller reports controller tracking lost while
  hand tracking is active. Debounce before showing so a momentary occlusion
  does not flash the toast — debounce duration: MEASURE ON DEVICE (tune against
  research question 3).
- **Exit:** Auto-dismiss after a few seconds, or immediately on controller
  reacquisition.
- **Animation constraint:** Fade in/out ≤200 ms. No motion, no scale animation.
- **Placement:** `comfortableUI` distance (1.5 m), low in the field of view so
  it does not cover content, head-facing. Exact vertical offset for PICO's
  FOV: MEASURE ON DEVICE.

## (c) Recenter affordance

- **States:**
  - `idle`: persistent "Recenter" button on the root panel.
  - `offered`: non-modal "Recenter?" toast after resume-from-background with
    head pose rotated >45° or translated >1 m (comfort skill §4).
  - `executing`: single-frame rebuild of the reference space
    (`recenterTracking()`); in Floor mode the rebuild preserves the floor
    offset per ADR-0001.
- **Entry:** Button press, toast accept, or long-press Home (PICO system
  recenter — handled via `REFERENCE_SPACE_CHANGE_PENDING`, ADR-0001).
- **Exit:** Instantaneous. The comfort skill forbids animating the UI's
  translation on recenter; the UI reappears in front of the user with at most
  a ≤200 ms fade.
- **Animation constraint:** ≤200 ms fade only. No animated translation,
  rotation, or scale — recenter must read as a cut, not a move.
- **Placement:** Button lives on the root panel at `comfortableUI` distance
  (1.5 m). Post-recenter the UI root lands 1.5 m in front of the current head
  pose at eye height, per the comfort skill's recenter pattern.
- **Note:** The recenter action moves the UI (and in native terms rebuilds the
  reference space); it never moves or rotates the user's viewpoint while
  visible.
