# Workstream B — controller-mesh wiring (device-gated follow-up)

The asset blocker is resolved: `android/sharedCode/src/main/assets/controller_neutral.glb`
is a neutral in-house 6DoF controller (ring + grip, 860 tris, `controller_body`
material). Metres; glTF +Y up / -Z forward (OpenXR grip-pose convention); origin
at the natural hold point (grip top third), so parenting at the grip action space
with an identity offset places it correctly.

What remains changes the input path and must be verified on a Quest 3 / PICO, so
it is written up here rather than committed blind (no "should work on device").

## Seams (verified on `pico-support`)

- Load a GLB from assets, native:
  `VROPlatformCopyAssetToFile("controller_neutral.glb")` → local path, then
  `VROGLTFLoader::loadGLTFFromResource(path, {}, VROResourceType::LocalFile,
   node, /*isGLTFBinary=*/true, driver, onFinish)`
  (`ViroRenderer/VROGLTFLoader.h:107`). Async — the node populates in `onFinish`.
  OBJ precedent is `VROInputPresenterDaydream.h:91-113` (`VROPlatformCopyAssetToFile`
  + `VROOBJLoader::loadOBJFromResource` + `addChildNode`).
- Presenter root + node parenting + the PR#369 unselectable pattern:
  `VROInputPresenterOpenXR.h:104-131` (`getOrCreateLaser`, `_rootNode->addChildNode`,
  `setSelectable(false)` / `setIgnoreEventHandling(true)` at :126-127). A per-hand
  mesh node hangs off `_rootNode` exactly like a laser.
- The presenter needs a `VRODriver` for the loader; it is created by
  `VROInputControllerOpenXR::createPresenter(driver)` — thread the driver into a
  `loadControllerMesh(driver)` call the way Daydream's presenter takes one.
- Input controller aim spaces located per frame: `_leftSpace`/`_rightSpace` are
  AIM pose action spaces (`VROInputControllerOpenXR` `createActionSpaces`); the
  laser follows aim via `updateAimRay`. There is NO grip pose action yet — only
  aim + the squeeze float `_rightGripAction`/`_leftGripAction`.

## Steps

1. Input controller: add `_leftGripPoseAction` / `_rightGripPoseAction`
   (`XR_ACTION_TYPE_POSE_INPUT`) + action spaces bound to
   `/user/hand/{left,right}/input/grip/pose` in EVERY suggested profile (Touch and
   every ByteDance profile). Aim keeps driving the beam; grip drives the mesh.
2. Presenter: `updateControllerMesh(source, worldPos, worldRot, visible)` — one
   per-hand node loaded from the GLB (mirror on X for the left hand), parented to
   `_rootNode`, `setSelectable(false)` + `setIgnoreEventHandling(true)`, material
   writes depth (tiled-GPU stereo, QUEST_SETUP §7b), hidden until positioned.
3. Per frame in `onProcess`: `xrGetActionStatePose(gripPoseAction)`; when
   `isActive == XR_FALSE` hide mesh AND beam within one frame. Also fix the beam
   to require `isActive` on the aim action, not `xrLocateSpace` flags alone.
4. Model selection by profile path (uses the `[XR-DIAG] active profile` log this
   branch already emits): `oculus/touch_controller`, `bytedance/*` → the neutral
   GLB; `khr/simple_controller` / `ext/hand_interaction_ext` → no mesh. One
   neutral model serves all today; per-vendor photoreal models are the upgrade.

## Acceptance (G2, on device)
Correct mesh per logged profile path; mesh appears ≤1 frame after
`INTERACTION_PROFILE_CHANGED`; set a controller down → `isActive` false → mesh +
beam gone ≤200 ms; zero hit-test hits on the mesh; both eyes render two meshes +
two beams within +0.3 ms/frame at 90 Hz.
