# Meta XR Simulator host rendering backend

## Problem

The current Horizon OpenXR renderer is an Android/Quest renderer. It requires `XR_KHR_opengl_es_enable`, initializes the Android OpenXR loader, creates EGL/OpenGL ES 3, binds the session with `XrGraphicsBindingOpenGLESAndroidKHR`, and consumes `XrSwapchainImageOpenGLESKHR`.

The standalone Meta XR Simulator is a desktop OpenXR runtime. The Meta VR Glasses profile must not be routed through the Android/GLES renderer. Meta XR Simulator supports Vulkan on Windows and macOS (and platform-specific D3D/Metal paths), and does not support OpenGL/OpenGL ES.

A black simulator viewport must therefore be classified across three independent failure boundaries, not hidden with clear-color or shader workarounds:

1. **Backend / render-target failure** — wrong graphics API, invalid swapchain usage, or pixels never land in the acquired image.
2. **Frame-submission failure** — the projection layer is not validly submitted/accepted.
3. **Accepted-but-black compositor/output failure** — swapchain pixels and an accepted projection layer exist, but the simulator compositor, capture path, or RemoteFrameObservation output is still black.

A Vulkan backend removes the known GLES incompatibility, but it does **not** by itself prove visible simulator output.

## Reproduced accepted-but-black case

A live simulator probe has already exercised the intended Vulkan path with `XR_KHR_vulkan_enable2`, runtime-mediated Vulkan instance/device creation, a 1680×1760×3 swapchain, RUNNING→FOCUSED session state, `shouldRender=true`, OPAQUE blend mode, and a projection layer accepted at approximately 60 fps.

Despite that, all observed compositor outputs remained black:
- Meta XR Operator MCP `openxr_capture_composited_image`: 840×880 PNG, zero non-black pixels;
- in-process debug eye viewport: black;
- standalone MetaXRSimulator RemoteFrameObservation viewport: black.

A Metal probe additionally verified that the swapchain texture itself contained magenta pixels while composited capture remained black. This demonstrates that “projection layer accepted” and “rendered pixels visible in compositor output” are separate gates.

## Required architecture

Keep the existing Android Quest path unchanged:

- Horizon device / Android -> current OpenXR + OpenGL ES backend.

Add a host simulator path:

- Meta XR Simulator / Windows or macOS -> OpenXR + Vulkan backend.
- Eskiu/XR backend selection must choose by host/runtime graphics capability, not by the simulated device marketing name.
- Never request `XR_KHR_opengl_es_enable` from the standalone simulator.
- Request `XR_KHR_vulkan_enable2` when exposed; fall back to `XR_KHR_vulkan_enable` only when necessary.
- Query Vulkan graphics requirements before `xrCreateSession`.
- Create the Vulkan graphics binding expected by the negotiated extension.
- Enumerate runtime swapchain formats and select a supported color format rather than assuming the Quest GLES sRGB format.
- Allocate one projection swapchain per view initially; multiview optimization can follow after correctness.
- Enforce acquire -> wait -> render -> release for every submitted image.
- Submit a projection layer only when view pose/orientation are valid and `shouldRender` is true.
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

5. release the rendered image;
6. submit `XrCompositionLayerProjection`;
7. assert successful frame completion, advancing frames/FPS, valid views, `shouldRender=true`, and that the simulator layer inspector reports the expected projection layer.

This gate is necessary but **not sufficient**.

### Gate C — the compositor produced visible pixels

8. capture the composited simulator output using Meta XR Operator `openxr_capture_composited_image` or `XR_METAX1_simulator_compositor_output_capture`;
9. run a pixel assertion over the captured image and require a non-black/non-zero result matching the smoke-test color/triangle;
10. compare that result with the in-process eye/debug viewport and RemoteFrameObservation output.

The smoke test passes only when **A + B + C** pass. A layer appearing in the Graphics panel alone is not a success condition.

### Gate C interpretation caveat

Treat the available simulator-side Gate C readers as correlated observations, not guaranteed independent proof of final display output. Meta XR Operator composited capture, the in-process debug eye viewport, and the RemoteFrameObservation frontend stream may share compositor/readback infrastructure and can therefore fail together if that observation path is stale, disconnected, or sampling a different image.

Accordingly:
- Gate A independently proves whether rendered pixels reached the application's acquired swapchain image.
- Gate B proves whether the runtime accepted the submitted projection layer.
- Gate C proves whether the simulator's observable compositor/capture path exposes those pixels.
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

## Non-goals

Do not solve this with CSS/DOM/WebGL fallbacks, screen capture, or a separate Unity/Unreal application. This is a native Viro/OpenXR backend correction.
