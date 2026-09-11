# PICO device-quality profiles

Two profiles following the `viroreact-device-quality-profiles` schema
(hardware, frame-rate target, budgets, UI density, passthrough, input,
fallback, testing, known constraints). Budgets marked MEASURE ON DEVICE have
not been measured on PICO hardware; do not ship numbers copied from the Quest
profiles.

## Capability matrix (per the PICO extension matrix at developer.picoxr.com)

| Capability | Neo3 / PICO 4 | PICO 4 Ultra |
|---|---|---|
| Hand tracking | yes | yes |
| `XR_EXT_hand_interaction` | no | yes |
| `XR_EXT_local_floor` | no | yes |
| `XR_EXT_palm_pose` | yes | yes |
| Plane detection (`XR_EXT_plane_detection`) | no | no |
| FB scene / spatial entity (`XR_FB_scene`, `XR_FB_spatial_entity*`) | no | no |
| Passthrough | yes | yes |

Consequences: the fork's AR plane path (EXT plane detection or FB scene, see
`VROSceneRendererOpenXR::createSession`) initializes on neither profile —
plane-dependent features need the graceful-degradation ladder. Floor mode uses
`XR_EXT_local_floor` only on 4 Ultra; Neo3/PICO 4 rely on the STAGE emulation
rung (ADR-0001).

## Profile A — PICO 4 Ultra (OS 5.x / 6)

**Hardware.** Snapdragon XR2 Gen 2, 12 GB RAM, color passthrough cameras —
per PICO public specs.

**Frame-rate target.** Panel supports 72 / 90 Hz per PICO public specs —
verify the enumerated set via `xrEnumerateDisplayRefreshRatesFB`. Sustained
FPS under representative scene load: MEASURE ON DEVICE.

**Visual fidelity target.** Comparable class of silicon to Quest 3 (XR2 Gen 2),
but treat the Quest 3 envelope as a hypothesis, not a budget, until measured.

**Texture budget.** MEASURE ON DEVICE.

**Polygon budget.** MEASURE ON DEVICE.

**Lighting / shadow budget.** MEASURE ON DEVICE (shadow-caster count under
sustained load).

**UI density.** Same `viroreact-spatial-layout-system` tokens as all profiles —
per-device gains go into content fidelity, not smaller type.

**Passthrough / MR.** Color passthrough available. No plane detection and no
FB scene (matrix above), so MR composition is unanchored to room geometry —
"place on your table" features must degrade to manual placement.

**Input.** Controllers, hand tracking, `hand_interaction`, `palm_pose`.

**Fallback behavior.** This is the top PICO tier; Profile B is its floor.

**Recommended testing.** 10-minute thermal-sustain session; both refresh
rates; hands-only and controllers-only sessions; Floor mode via native
`local_floor`; system recenter (long-press Home) during a session.

**Known constraints.** Only PICO device with `local_floor` and
`hand_interaction` — features gated on either need the Profile B fallback.
Foveation enum spellings and the OS version property key still need
verification on hardware (see PICO-SUPPORT.md, "Build boundary").

## Profile B — PICO Neo3 / PICO 4 (OS 5)

**Hardware.** Snapdragon XR2 Gen 1 (both), per PICO public specs. RAM: 6 GB
(Neo3) / 8 GB (PICO 4), per PICO public specs.

**Frame-rate target.** Panels support 72 / 90 Hz per PICO public specs —
verify via `xrEnumerateDisplayRefreshRatesFB` per device. Sustained FPS under
load: MEASURE ON DEVICE.

**Visual fidelity target.** Design-to-the-floor tier for PICO. XR2 Gen 1 is
the Quest 2 silicon class; treat the Quest 2 envelope as a starting hypothesis
and measure.

**Texture budget.** MEASURE ON DEVICE (Neo3's 6 GB RAM is the binding case).

**Polygon budget.** MEASURE ON DEVICE.

**Lighting / shadow budget.** MEASURE ON DEVICE. Bake lighting by default.

**UI density.** Same layout tokens. Do not shrink type on the older panels.

**Passthrough / MR.** Passthrough available per the matrix. Color vs greyscale
quality per model: OPEN QUESTION — verify on hardware before designing MR
scenes that depend on color perception. No plane detection, no FB scene.

**Input.** Controllers, hand tracking, `palm_pose`. No `hand_interaction` —
hand input must work through the base hand-tracking path.

**Fallback behavior.** This is the PICO floor. Floor mode has no
`local_floor`; it depends entirely on the STAGE emulation rung, and if STAGE
is not locatable the app stays in Eye mode (ADR-0001 rung 3).

**Recommended testing.** All Profile A tests minus native `local_floor`;
verify the emulated floor offset against the physical floor (tolerance from
research question 2); controller pick-up/put-down hand-off; OS 5 vs OS 5.9+
behavior differences noted in PICO-SUPPORT.md.

**Known constraints.** No `local_floor`, no `hand_interaction`, no plane or
scene understanding. Whether STAGE is enumerated and locatable here is the
open question ADR-0001's emulation rung hangs on — test it first.
