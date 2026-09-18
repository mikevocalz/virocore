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

---

# As-built — S1 to S5

Sections (a)–(c) above spec surfaces that are still being designed. This section
describes what the PICO/Quest build actually renders today, measured from source.
Where a surface is specified but not implemented, that is stated rather than
drawn.

All five live in the Viro fork. File references without a repo prefix are
[viro @ `codex/pico-cli-bridge`](https://github.com/mikevocalz/viro/tree/codex/pico-cli-bridge);
renderer references are prefixed `virocore`. Copy strings are in `ux-copy.md`.

## Measurement basis

Two constants govern every number below.

**Text world size.** `ViroText` converts `fontSize` to metres with a fixed
factor: `kTextPointToWorldScale = 0.01`
(virocore `ViroRenderer/VROText.h:44`). `fontSize: 16` is a 0.16 m em box, not
16 of anything on a screen. Nothing in the JS layer scales this by distance.

**Head-lock geometry.** `computeHeadLockedTransform`
(`components/Studio/domain/questHeadLockedTransform.ts:30-45`) places a panel at
`distanceM` along the camera forward vector, then adds `verticalOffsetM` along
the up vector, and yaws it back toward the viewer. Pitch and roll stay at zero
(`:43-44`), so the panel is always upright. Defaults are `distanceM = 1.2`,
`verticalOffsetM = 0` (`:34`).

The offset is applied along `up`, not subtracted from the forward distance, so a
non-zero `verticalOffsetM` increases the true viewing distance. For the HUD's
`verticalOffsetM: -0.4` the panel sits at `sqrt(1.2² + 0.4²)` = **1.265 m**, and
**18.4°** below the gaze axis. Angular sizes below use 1.265 m for S3 and
1.200 m for S2.

`distance.comfortableUI` is 1.25–2.0 m. S2 at 1.200 m is just under that floor;
S3 at 1.265 m is just inside it.

## S1 — Pre-launch permission request and denied state

`components/ViroXRSceneNavigator.tsx:36-40, 252-289`

Three permissions are requested in one call before VRActivity launches:
`horizonos.permission.USE_ANCHOR_API`, `com.oculus.permission.USE_SCENE`,
`horizonos.permission.HEADSET_CAMERA` (`:36-40`).

**Quest only.** The request is wrapped in `if (isQuest)` (`:268`), with the
comment that Horizon permissions do not exist on PICO and that PICO's runtime and
app config own their own. No PICO equivalent is requested anywhere in this file.

**States as built.** One: requested. The call is
`await PermissionsAndroid.requestMultiple(QUEST_RUNTIME_PERMISSIONS as any).catch(() => undefined)`
(`:269-270`). The resolved value is the grant map, and it is discarded. Nothing
reads it, nothing branches on it, and `launch()` continues to `setIntent` and
`launchVRScene()` identically whether every permission was granted or every one
was denied.

**The denied state does not exist in code.** There is no denied branch, no
re-request path, no explanatory surface, and no signal to JS. The copy for it is
written in `ux-copy.md` and marked as new.

**This is a functional defect, not only a missing screen.** Quest plane detection
is gated on that grant. The upstream platform matrix lists both plane-detection
rows as "Quest 3/3S via XR_FB_scene room model (Space Setup +
`horizonos.permission.USE_ANCHOR_API`)", and the Quest setup guide §7b states the
permission is runtime-granted and must be requested in-app. A user who denies it
gets a session where plane detection cannot work, and the only downstream signal
is the same `planeDetection: false` that a PICO device produces for an unrelated
reason.

**Timings.** The request is awaited, so VRActivity launch is blocked for as long
as the system dialog is on screen. `cancelled` is checked immediately after
(`:272`), so unmounting during the dialog aborts the launch cleanly.

**PICO vs Quest.** PICO requests nothing here and has no system dialog in this
path. The PICO manifest contract is supplied by expo-pico
(`app/src/pico/AndroidManifest.xml`, per expo-pico
`docs/VIRO-PICO-INTEGRATION.md:37`), which means a PICO permission failure
surfaces at install or at first native use, not at this point in the JS lifecycle.

## S2 — In-scene alert

`components/Studio/StudioQuestAlertOverlay.tsx`,
`components/Studio/domain/questAlertStore.ts`

Replaces `Alert.alert`, which renders nothing in the VR compositor. Mounted from
`components/Studio/StudioARScene.tsx:1426`, gated on `isXRHeadset`.

**States.** Two, held in a module singleton. `questAlertStore.isActive()` is
`activeMessage !== null` (`questAlertStore.ts:15-17`); `show(title, message)`
sets it (`:31-35`) and `dismiss()` clears it (`:37-42`). `reset()` is an alias
for `dismiss()` (`:45-47`), called on scene teardown so a stale alert cannot
carry into the next scene.

A falsy title is normalised to `null` (`:32`) and the title `ViroText` is then
skipped entirely (`StudioQuestAlertOverlay.tsx:50-63`), which changes the panel's
vertical composition without changing its 1 m height.

**Only one alert exists at a time.** `show()` overwrites whatever was active with
no queue and no notification that a message was replaced.

**Geometry at 1.200 m.**

| Element | Source | Metres | Angular |
|---|---|---|---|
| Panel | `:40-41` | 2.00 × 1.00 | 79.6° × 45.2° |
| Padding | `:47` | 0.06 | — |
| Title em box | `:53-54, 57` | 1.80 × 0.30, `fontSize: 22` → 0.22 | 10.5° tall |
| Message em box | `:66-67, 70` | 1.80 × 0.50, `fontSize: 16` → 0.16 | 7.6° tall |

Content height when a title is present is 0.30 + 0.50 = 0.80 m inside a
0.88 m content box (1.00 − 2 × 0.06). Without a title it is 0.50 m in the same
box, and `justifyContent: "center"` (`:45`) re-centres it, so the message jumps
vertically between the titled and untitled cases.

**Dismissal.** A controller click anywhere on the `ViroFlexView` (`:42`). There
is no visible button, no hover state, and no hit-target boundary other than the
panel edge. Mirrors tapping "OK", except that the target is the whole 2 × 1 m
panel.

**Timings.** None. No fade, no delay, no auto-dismiss. The panel appears and
disappears on the frame the store changes. Section (b) above mandates a ≤200 ms
fade for the toast; this surface does not have one.

**Edge cases.**
- `cameraPose` null returns `null` (`:31`), so no alert can render before the
  first `onCameraTransformUpdate`. An alert raised during scene load is held in
  the store and appears when the first pose arrives.
- The panel re-renders on every cached pose update. The pose is throttled in
  `StudioARScene`; the throttle interval is not specified here.
- `message ?? ""` (`:65`) renders an empty panel rather than nothing if `show()`
  is ever called with an empty message.

**PICO vs Quest.** Identical. The gate is `isXRHeadset`, not `isQuest`.

## S3 — Scene and exit HUD

`components/Studio/StudioQuestSceneHudOverlay.tsx`

Persistent head-locked strip carrying the scene name, plane status, and the only
visible exit. Mounted from `StudioARScene.tsx:1427-1435`, gated on `isXRHeadset`.
Positioned below S2 deliberately (`:41-42`) so a simultaneous alert does not
cover it.

**States.** The panel itself has one. The plane-status line has four:

| Condition | Source | Line |
|---|---|---|
| `planeDetectionMode === "NONE"` | `:80` | hidden entirely |
| `!planeDetectionAvailable` | `:82` | "Surface tracking unavailable — use manual placement" |
| `hasFoundPlane` | `:82` | "Plane found" |
| otherwise | `:82` | "Scanning for planes…" |

`planeDetectionAvailable` is computed at `StudioARScene.tsx:1239-1240` as
`!isVisionOS && (!isXRHeadset || xrCapabilities?.planeDetection === true)`. On a
headset this is false while the capability is still `null`, and
`useOpenXRCapabilities` polls for up to 20 attempts at 250 ms
(`components/Utilities/useOpenXRCapabilities.ts:26-27`). For up to five seconds
after mount the HUD asserts "Surface tracking unavailable" on a device where it
is in fact still resolving.

**Geometry at 1.265 m.**

| Element | Source | Metres | Angular |
|---|---|---|---|
| Panel | `:60-61` | 1.60 × 0.50 | 64.6° × 22.3° |
| Padding | `:66` | 0.04 | — |
| Scene name em box | `:71-72, 75` | 1.50 × 0.15, `fontSize: 14` → 0.14 | 6.3° tall |
| Plane status em box | `:83-84, 87` | 1.50 × 0.12, `fontSize: 11` → 0.11 | 5.0° tall |
| Exit em box / hit strip | `:95-96, 100` | 1.50 × 0.15, `fontSize: 13` → 0.13 | 61.3° × 6.8° |

**The content box is an exact fit with zero slack.** Content height is
0.50 − 2 × 0.04 = 0.42 m. Children are 0.15 + 0.12 + 0.15 = 0.42 m. There is no
room for a gap between the three rows, no room for a descender to overflow its
box, and no room to add a fourth row without changing the panel height. Across
the width the margin is 0.02 m total (1.52 content, 1.50 children), 0.01 m per
side.

Hiding the plane-status row leaves 0.12 m of slack, which `justifyContent:
"center"` distributes as leading — so the two remaining rows sit further apart in
`NONE` mode than in any other mode.

**Exit is a 1.5 × 0.15 m invisible hit strip on a destructive action with no
confirmation** (`:93-97`). The `onClick` is on the `ViroText`, so the target is
the text geometry's full 1.5 m width, 61.3° of arc, and nothing renders its
boundary. `handleExitClick` (`:26-29`) calls the intent's `onExitViro` and then
`exitVRScene()` immediately. There is no confirm step, no undo, and no
distinction between a deliberate press and a stray controller ray that crosses
the strip.

**Timings.** None. Exit is immediate on click.

**Edge cases.**
- `cameraPose` null returns `null` (`:50`), so the HUD — and with it the only
  visible exit — is absent until the first camera pose arrives. The hardware back
  button covers the same path and is available earlier.
- `sceneName ?? "Untitled scene"` (`:70`).
- No truncation. A long scene name is laid out into a 1.5 × 0.15 m box by
  `ViroText`; the wrap and clip behaviour at that box size is not established
  here.

**PICO vs Quest.** Same component, same gate. The plane-status line is where they
diverge in practice: on Quest the `true` case is reachable via the room model,
and on PICO it is predicted unreachable (see S4).

## S4 — Missing-plane fallback

`components/Utilities/useOpenXRCapabilities.ts`,
`components/Studio/StudioARScene.tsx`

**Capability source.** `getCapabilities(tag)` resolves `{ planeDetection }` from
the native atomic. The native side sets it once, at session creation:
`_planeDetectionStatus.store(_arSession ? 1 : 0)` at
virocore `android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.cpp:619`, after
trying two plane sources — `XR_EXT_plane_detection`, or the full
`XR_FB_scene` + `XR_FB_spatial_entity` + `XR_FB_spatial_entity_query` trio
(`:603-617`). It is reset to −1 in `destroySession()` (`:1289`).

**States.** Three at the JS boundary, and the third is overloaded.

| `planeDetection` | Meaning | Rendered |
|---|---|---|
| `null` | session pending, **or** the native call failed | treated as unavailable |
| `false` | session up, no plane source initialised | unavailable copy, manual placement |
| `true` | a plane source initialised | scanning / found |

`null` carries two unrelated meanings. The hook's `catch` returns
`{ planeDetection: null }` (`:29-33`) for every native rejection, and the native
module rejects with three distinct codes —
`E_XR_VIEW_UNAVAILABLE`, `E_XR_CAPABILITIES`, and `E_XR_REBUILD_REQUIRED`
(`android/viro_bridge/src/main/java/com/viromedia/bridge/module/VRModuleOpenXR.java:63, 73, 75`).
`E_XR_REBUILD_REQUIRED` is raised from a `LinkageError`, which is exactly what an
app built against a Viro release that predates this API produces. That case is
indistinguishable from "still initialising" in the UI.

**Behaviour when unavailable.** `anchorDetectionTypes` returns `[]`
(`StudioARScene.tsx:1253`), so no native plane scanning starts, and `renderAssets`
returns the assets unwrapped rather than inside a `ViroARPlane`
(`:1345-1347`). Content still renders; it is simply not anchored. Controller
placement stays available.

**`true` is predicted unreachable on PICO.** `device-profiles.md:17-25` records
no PICO tier — Neo3, PICO 4, or PICO 4 Ultra — enumerating either
`XR_EXT_plane_detection` or the FB scene trio, so the `if` at
`VROSceneRendererOpenXR.cpp:605` is never entered and `_arSession` stays null.
Treat the `null → true` transition as a Quest-only path until a runbook case
observes otherwise on hardware.

**Capability and anchors are decoupled, and the UI reads only capability.**
`_planeDetectionStatus` is set at session creation regardless of what scene root
mounts. Anchors travel a different path: `attachARSceneIfNeeded` does
`dynamic_pointer_cast<VROARScene>` and returns early for a plain `VROScene`
(`VROSceneRendererOpenXR.cpp:1236-1241`). Upstream viro#526's merge
[`5bddce6`](https://github.com/mikevocalz/viro/commit/5bddce6) kept the Quest
scene root at `ViroScene` and says so in the message: "with this root a Studio
scene on Quest gets no detected surfaces."

So a Quest build can report `planeDetection: true`, render "Scanning for planes…"
forever, and never deliver an anchor — and nothing on screen distinguishes that
from a device that simply has not found a plane yet. Any runbook case covering
this surface must record the capability value **and** the anchor outcome
separately, or it cannot tell the two builds apart.

**Timings.** Poll starts on mount once `tag` is non-null
(`useOpenXRCapabilities.ts:17`), retries every 250 ms while the value is `null`,
gives up after 20 attempts (`:26-27`) — a five-second ceiling, after which a
still-pending session is permanently reported as unavailable with no retry.

**PICO vs Quest.** Quest reaches `true` through `XR_FB_scene` when Space Setup
has been run and `USE_ANCHOR_API` is granted — the grant that S1 discards. PICO
is expected to sit at `false` on every tier.

## S5 — 2D launch panel

`components/ViroXRSceneNavigator.tsx:320-323`,
`components/Studio/StudioSceneNavigator.tsx:544-615`

**`ViroXRSceneNavigator` renders nothing on a headset.** `if (isQuest || isPico)
return null` (`:323`). VRActivity owns the display; MainActivity stays a 2D panel
so no React Native UI bleeds into the XR scene.

**What the panel actually contains on a headset.** `StudioSceneNavigator` wraps
the navigator in a `View` with `StyleSheet.absoluteFill` (`:544`) and four
conditional siblings: the error overlay (`:591`), the recording indicator
(`:592-599`), the placement overlay — which is explicitly `!isXRHeadset`
(`:600-606`) — and the placement banner (`:607-614`). On a headset, with no error
and nothing recording, every one of those is absent and the navigator itself
returns null. The panel is an empty full-screen `View`.

**States as built.** One: empty. There is no launching state, no "headset is
running this scene" state, and no state for the window between `exitVRScene()`
and the host app navigating away. `onExitViro` is the app's responsibility
(`:582`); if the host does not navigate, the user is returned to a blank screen.

**Timings.** VRActivity launch is blocked on the S1 permission dialog on Quest.
Re-launch on background→active is debounced against the dual-Activity lifecycle
using `leftActiveAtRef` (`ViroXRSceneNavigator.tsx:214-220`), which distinguishes
a genuine system background (seconds) from an Activity-transition bounce
(under 500 ms).

**Edge cases.**
- Missing scene: `console.warn` and return, with no launch and nothing on the
  panel (`:256-259`).
- Missing launcher: throws (`:260-262`). The throw is caught by the outer
  `launch().catch` (`:282-289`), which clears the VR-active flag and reports via
  `VRQuestNavigatorBridge.reportQuestError`.

**Never cold-start `.VRActivity` directly.** It is declared
`android:exported="false"` (`plugins/withViroAndroid.ts:774`), and launching it
from a cold app bypasses intent setup entirely — the activity mounts with no
scene registered. expo-pico `docs/VIRO-PICO-INTEGRATION.md:35` says the same.
Launch the package and let the panel set the intent.

**PICO vs Quest.** The null-render branch covers both (`:323`). The launch effect
covers both (`:253`). They differ only in the permission step (Quest only,
`:268`) and in `hdrEnabled`.

## Renderer flag that differs by device

`StudioSceneNavigator.tsx:580` passes `hdrEnabled={!isQuest}`, landed in
[`f0a51bd`](https://github.com/mikevocalz/viro/commit/f0a51bd).

Off on Quest because the HDR composite renders to an intermediate target and
forces an opaque final composite, which hides the passthrough layer. Upstream
documents this as a requirement rather than a preference: the Quest setup guide
§7b says to set `hdrEnabled={false}` for MR scenes, and the platform matrix's
mixed-reality row repeats it.

It stays on for PICO. That guidance does not cover PICO, and turning HDR off
costs PBR outright — `VROChoreographer::isPBREnabled` is
`_hdrEnabled && _pbrEnabled`, so roughness, metalness and the AO map stop being
read and materials fall back to Blinn. Widening the condition to `!isXRHeadset`
without observing PICO's compositor would be a guess. Runbook case K07 settles it.
