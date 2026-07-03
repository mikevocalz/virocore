# Upstream PR to open — ReactVision/virocore

## What
Cherry-pick commit `0d732997` ("fix(openxr): create passthrough layer paused —
fixes PICO passthrough") from `mikevocalz/virocore` branch `v2.55.0-nitro-canvas`
onto a clean branch off `ReactVision/virocore` main.

File: `android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.cpp`
(initPassthrough — layer creation flags).

## Suggested PR title
fix(openxr): create passthrough layer paused — fixes passthrough on PICO 4 Ultra

## Suggested PR body

XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB + an immediate
xrPassthroughLayerPauseFB leaves the layer in a state PICO's
XR_FB_passthrough implementation rejects: the init-time pause fails
("Passthrough feature is not started, pause passthrough layer may not
work") and every subsequent xrEndFrame that includes the layer is refused
("PassthroughFB-submit_passthrough_layer_effect passthrough layer count is
illegal"), so passthrough never composites — the scene renders over black.

Creating the layer with `flags = 0` (paused) and resuming it only after
`xrPassthroughStartFB` (which setPassthroughEnabled already does) is the
canonical sequence and behaves identically on Quest.

Verified on-device:
- PICO 4 Ultra (PICO OS 5.x, runtime 2.2.0, XR_FB_passthrough advertised):
  passthrough composites, room visible behind scene content, layer errors gone.
- Quest path unchanged: same start→resume order as before; only the
  create-running+pause dance is removed.

Also verified: works with `hdrEnabled={false}` per the 2.57.2 upgrade notes.

## Steps when opening
```bash
cd ~/virocore
git fetch origin
git checkout -b fix/pico-passthrough-layer-paused origin/main
git cherry-pick 0d732997
git push fork fix/pico-passthrough-layer-paused
gh pr create --repo ReactVision/virocore \
  --head mikevocalz:fix/pico-passthrough-layer-paused \
  --title "fix(openxr): create passthrough layer paused — fixes passthrough on PICO 4 Ultra"
```
