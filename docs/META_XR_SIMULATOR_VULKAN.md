# Meta XR Simulator host rendering backend

## Problem

The current Horizon OpenXR renderer is an Android/Quest renderer. It requires `XR_KHR_opengl_es_enable`, initializes the Android OpenXR loader, creates EGL/OpenGL ES 3, binds the session with `XrGraphicsBindingOpenGLESAndroidKHR`, and consumes `XrSwapchainImageOpenGLESKHR`.

The standalone Meta XR Simulator is a desktop OpenXR runtime. The Meta VR Glasses profile must not be routed through the Android/GLES renderer. Meta XR Simulator supports Vulkan on Windows and macOS (and platform-specific D3D/Metal paths), and does not support OpenGL/OpenGL ES.

A black simulator viewport must therefore be classified across three independent failure boundaries, not hidden with clear-color or shader workarounds:

1. **Backend / render-target failure** — wrong graphics API, invalid swapchain usage, or pixels never land in the acquired image.
2. **Frame-submission failure** — the projection layer is not validly submitted/accepted.
3. **Accepted-but-black compositor/output failure** — swapchain pixels and an accepted projection layer exist, but the simulator compositor, capture path, or RemoteFrameObservation output is still black.

A Vulkan backend removes the known GLES incompatibility, but it does **not** by itself prove visible simulator output.

## Reproduced accepted-but-black case — resolved

A live simulator probe exercised the intended Vulkan path with `XR_KHR_vulkan_enable2`, runtime-mediated Vulkan instance/device creation, a 1680×1760×3 swapchain, RUNNING→FOCUSED session state, `shouldRender=true`, and a projection layer accepted at approximately 60 fps — while all compositor outputs remained black (operator `openxr_capture_composited_image`, in-process debug eye viewport, and RemoteFrameObservation frontend stream). The Metal path reproduced the same signature with a verified-magenta swapchain texture.

**The cause was app-side, not a simulator compositor defect.** The probe populated `XrCompositionLayerProjectionView.pose` and `.fov` with fabricated constants (identity pose, hard-coded FOV). Meta XR Simulator silently discards projection views whose pose/FOV were not produced by `xrLocateViews` for the frame's `predictedDisplayTime` — the layer is *accepted*, `layer list` reports it, `shouldRender` is true, and nothing in the API surface warns that the views were dropped. After switching to located views, composited output immediately produced the expected magenta on both Vulkan and Metal paths under default config (`gpu_handle` transport, compositor enabled, interop active). Earlier `jpg`/`disable_interop`/`disable_compositor` toggles were all red herrings.

So the Vulkan host path is necessary **and** sufficient for this simulator — provided view poses are located, not fabricated.

Follow-up verification: with located views, the probe now draws a real triangle through a `VkPipeline` (clear + `vkCmdDraw`, not just `vkCmdClearColorImage`) and the composited capture shows the rendered gradient — Gate A verified through the graphics pipeline, Gate C verified end-to-end on both backends. The probe also subscribes to `XR_EXT_hand_tracking` (`xrCreateHandTrackerEXT`/`xrLocateHandJointsEXT`), which is required before the operator layer's synthetic-input tools report delivery: `openxr_hand_gesture`/`openxr_gaze_and_pinch` previously failed with "the application never observed it" and now return `success: true` with `hand R active=1` observed by the app. `XR_EXT_eye_gaze_interaction` is present on this runtime.

## Required architecture

Keep the existing Android Quest path unchanged:

- Horizon device / Android -> current OpenXR + OpenGL ES backend.

Add a host simulator path:

- Meta XR Simulator / Windows or macOS -> OpenXR + Vulkan backend; on macOS `XR_KHR_metal_enable` is an equally valid and substantially cheaper target because the repo already ships a Metal substrate (visionOS), whereas no Vulkan substrate exists yet — Vulkan remains the right choice for Windows parity.
- Eskiu/XR backend selection must choose by host/runtime graphics capability, not by the simulated device marketing name.
- Never request `XR_KHR_opengl_es_enable` from the standalone simulator.
- Request `XR_KHR_vulkan_enable2` when exposed; fall back to `XR_KHR_vulkan_enable` only when necessary.
- Query Vulkan graphics requirements before `xrCreateSession`.
- Create the Vulkan graphics binding expected by the negotiated extension.
- Enumerate runtime swapchain formats and select a supported color format rather than assuming the Quest GLES sRGB format.
- Allocate one projection swapchain per view initially; multiview optimization can follow after correctness.
- Enforce acquire -> wait -> render -> release for every submitted image.
- Submit a projection layer only when `shouldRender` is true, and populate every `XrCompositionLayerProjectionView.pose` and `.fov` from `xrLocateViews` at that frame's `predictedDisplayTime`. Fabricated view poses are silently discarded by this runtime — the layer is accepted but never composited, producing black output that looks exactly like a compositor failure.
- Keep passthrough/environment blend behavior capability-driven.

## Black-frame diagnostic gates

Before Viro scene rendering, the host backend must prove all three stages independently.

### Gate A — pixels reached the swapchain

1. create OpenXR instance/system/session/reference space;
2. create Vulkan swapchains with usage flags compatible with the chosen test path;
3. render distinct opaque colors or, preferably, a minimal unlit triangle through a graphics pipeline;
4. verify pixels actually landed in the acquired swapchain image before release.

Do not assume `vkCmdClearColorImage` worked: if the runtime did not grant `TRANSFER_DST` usage, a clear-based test can silently produce a misleading result. Prefer graphics-pipeline rendering and add readback when the image/format/usage permits it.

### Gate B — the runtime accepted the projection layer

5. call `xrLocateViews` with the frame's `predictedDisplayTime` and copy the returned `pose`/`fov` into every submitted projection view — never fabricate them;
6. release the rendered image;
7. submit `XrCompositionLayerProjection`;
8. assert successful frame completion, advancing frames/FPS, `shouldRender=true`, and that the simulator layer inspector reports the expected projection layer.

This gate is necessary but **not sufficient** — and it can pass while views are being silently discarded. An accepted projection layer with fabricated poses is the known false-positive for this gate: the layer inspector reports it and `xrEndFrame` succeeds, but the compositor never samples the swapchain.

### Gate C — the compositor produced visible pixels

9. capture the composited simulator output using Meta XR Operator `openxr_capture_composited_image` or `XR_METAX1_simulator_compositor_output_capture`;
10. run a pixel assertion over the captured image and require a non-black/non-zero result matching the smoke-test color/triangle;
11. compare that result with the in-process eye/debug viewport and RemoteFrameObservation output.

Proven working on this simulator: with located views, `openxr_capture_composited_image` returns the submitted swapchain content (840×880 capture of a magenta-cleared 1680×1760 swapchain, all pixels `ff00ff`) on both Vulkan and Metal paths.

The smoke test passes only when **A + B + C** pass. A layer appearing in the Graphics panel alone is not a success condition.

### Gate C interpretation caveat

Treat the available simulator-side Gate C readers as correlated observations, not guaranteed independent proof of final display output. Meta XR Operator composited capture, the in-process debug eye viewport, and the RemoteFrameObservation frontend stream may share compositor/readback infrastructure and can therefore fail together if that observation path is stale, disconnected, or sampling a different image.

Accordingly:
- Gate A independently proves whether rendered pixels reached the application's acquired swapchain image.
- Gate B proves whether the runtime accepted the submitted projection layer.
- Gate C proves whether the simulator's observable compositor/capture path exposes those pixels.
- Before classifying a Gate C failure as compositor-side, verify Gate B's view-pose requirement: this runtime silently drops projection views whose pose/FOV were not produced by `xrLocateViews`, and that discard is indistinguishable from a compositor defect at the capture layer.
- Unanimous black Gate C results do **not**, by themselves, prove that a physical HMD would display black.

When possible, validate the same minimal scene on physical hardware before classifying an accepted-but-black simulator result as a renderer defect. Preserve the A/B/C evidence separately so a simulator capture defect cannot be mistaken for a Viro rendering failure.

When GPU interop/capture is suspected, also test the simulator session texture transport configuration in both `gpu_handle` and `jpg`/CPU-copy modes. Record whether the failure follows the GPU-handle path or remains black in CPU-copy transport.

Only after these gates pass should Viro scene rendering be blamed or enabled as the next diagnostic layer.

Log at startup:
- runtime name/version
- OpenXR API version
- host OS
- selected graphics backend
- enabled graphics extension
- view count and recommended dimensions
- selected swapchain format
- environment blend mode

Log failures for every acquire/wait/release/end-frame operation. Do not silently continue after an invalid/incomplete render target.

## Renderer boundary

Do not force the existing `VRODriverOpenGLAndroidOpenXR` / `VRODisplayOpenGLOpenXR` classes to impersonate a Vulkan backend. Introduce a host OpenXR renderer/driver/display boundary so Quest GLES and simulator Vulkan can evolve independently while sharing higher-level Viro scene/input contracts.

Target structure:

```
OpenXR runtime
  -> graphics capability negotiation
      -> Android/Horizon: GLES renderer
      -> Desktop XR Simulator: Vulkan renderer
  -> shared Viro scene/input contracts
```

The Eskiu migration should consume the same backend contract rather than creating a third simulator-specific rendering architecture.

## Acceptance gates

- `hello_xr` renders against the same active Meta XR Simulator runtime.
- Meta VR Glasses profile passes swapchain-pixel, accepted-layer, and composited-pixel gates with no black frame.
- Minimal Viro scene renders in both eyes.
- Head pose updates view matrices.
- Restart after changing simulator device profile works.
- Projection layer is visible in the simulator Graphics panel **and** compositor capture contains the expected non-black pixels.
- No GL/GLES graphics extension is requested on the desktop simulator path.
- Existing physical Quest/Horizon GLES path remains green.
- Passthrough-off immersive scene works before MR/passthrough is enabled.
- MR/passthrough failure degrades visibly and logs the capability/format failure rather than returning an unexplained black frame.

### Status (2026-10-07, macOS, Meta XR Simulator 207.0.0, Metal path)

`desktop/tools/xr_sim_gates.py` runs the gates below against a live simulator for each device profile: render and input gates on both `ses_texture_format` transports, the three display modes, and `hello_xr`. It exits non-zero on a failure. Last run: Meta VR Glasses and Meta Quest 3, all passing.

| Gate | Status |
|---|---|
| Swapchain pixels (A) | Verified. Host reads back eye 0 and counts lit pixels; 232,812 of 2,956,800 on Glasses. |
| Accepted layer (B) | Verified. Views come from `xrLocateViews`; frames advance at FOCUSED. |
| Composited pixels (C) | Verified through `openxr_capture_composited_image`, both eyes, both transports. |
| Simulator window (eye viewport, Graphics panel, RemoteFrameObservation stream) | Blocked by the simulator. The window drops every session 5 s after connecting (`Frontend is disconnected. XrSession synchronization ... cancelled`), for `hello_xr` as well as `viro_sim_host`, and it crashed once in `RuntimeOrchestrator::transferToNewState` while handling a disconnect (`MetaXRSimulator-2026-10-07-113737.ips`, SIGSEGV). The operator capture is the Gate C evidence until Meta fixes the window. |
| Viro scene in both eyes | Verified, with left/right centroid disparity. |
| Head pose updates view matrices | Verified. A 20° yaw left moves the box about 200 px right in the left eye. |
| Restart after profile change | Verified, Glasses to Quest 3 and back. |
| No GL/GLES extension requested | Verified. |
| Input | Verified. Controller aim + trigger hits the box on both profiles; gaze + pinch hits it on Glasses. Quest 3 reports no eye gaze (`XR_ERROR_PATH_UNSUPPORTED` on the eye-gaze profile), and the host falls back to the aim pose. |
| `hello_xr` on the same runtime | Verified on both profiles. Metal plugin, session reaches FOCUSED, both eyes show the cubes, no XR errors. The simulator composites Metal through its Vulkan compositor (`RenderingMetalOnVulkan`). |
| Quest/Horizon GLES path still green | CI `androidBuild` passes. Needs a Quest on USB to run; none was connected. |
| Passthrough-off immersive scene | Verified. Default mode is immersive: blend OPAQUE, black background. |
| Passthrough | Verified. `VIRO_PASSTHROUGH=1` creates an `XR_FB_passthrough` reconstruction layer under the projection layer, selects ALPHA_BLEND and clears to transparent; the simulator room shows behind the box. |
| Passthrough degradation | Verified. With the extension hidden (`VIRO_XR_DISABLE_EXT=XR_FB_passthrough`) the host logs `passthrough unavailable: XR_FB_passthrough not enabled; falling back to immersive`, selects OPAQUE and clears to slate, so the fallback never looks like a black frame. A system without ALPHA_BLEND takes the same path with its own reason. |

Simulator traps the script handles, both of which silently produce the wrong device:
- The config env var is `META_XRSIM_CONFIG_JSON`. Any other name is ignored and the simulator runs on its bundled `config/sim_core_configuration.json`.
- `device_profile` in `~/Library/Application Support/MetaXR/MetaXrSimulator/persistent_data.json` overrides the config file. The script writes the profile there per run and restores the file afterwards.

The simulator's Meta VR Glasses profile squashes everything horizontally to about 0.85. It locates a ±37°×±34° FOV (tangent span 1.507×1.349, ratio 1.117) but recommends a 1680×1760 swapchain (ratio 0.955), so each pixel covers 0.855 times as much angle horizontally as vertically. `hello_xr` shows the same squash, and both apps draw square shapes on Quest 3, whose FOV and swapchain agree. Apps render correctly per spec. The inconsistency is in the profile. The host logs `eye N fov ... tan span ... vs swapchain ...` at startup so a mismatch is visible.

Quest 3's eye FOVs are asymmetric, so the box sits off-center in each eye. A center-pixel check reads black there on a correct frame; the readback gate counts lit pixels across the whole image instead.

## Future: Vulkan backend for Windows

macOS uses the Metal backend (`XR_KHR_metal_enable`), and every gate above passes on it. A Vulkan host backend matters only for running the simulator on Windows, which this project doesn't do today. When it does, the requirements in "Required architecture" still apply: `XR_KHR_vulkan_enable2`, graphics requirements before `xrCreateSession`, runtime-chosen swapchain format, and located view poses. The gate runner's checks carry over unchanged; only the host binary differs.

## Non-goals

Do not solve this with CSS/DOM/WebGL fallbacks, screen capture, or a separate Unity/Unreal application. This is a native Viro/OpenXR backend correction.
