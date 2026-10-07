# Meta XR Simulator host rendering backend

## Problem

The current Horizon OpenXR renderer is an Android/Quest renderer. It requires `XR_KHR_opengl_es_enable`, initializes the Android OpenXR loader, creates EGL/OpenGL ES 3, binds the session with `XrGraphicsBindingOpenGLESAndroidKHR`, and consumes `XrSwapchainImageOpenGLESKHR`.

The standalone Meta XR Simulator is a desktop OpenXR runtime. The Meta VR Glasses profile must not be routed through the Android/GLES renderer. Meta XR Simulator supports Vulkan on Windows and macOS (and platform-specific D3D/Metal paths), and does not support OpenGL/OpenGL ES.

A black simulator viewport must therefore be treated as a backend-selection / frame-submission failure, not hidden with clear-color or shader workarounds.

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

## Black-frame diagnostic gate

Before Viro scene rendering, the host backend must support a deterministic compositor smoke test:

1. create OpenXR instance/system/session/reference space;
2. create Vulkan swapchains;
3. render distinct opaque colors or a minimal unlit triangle per eye;
4. release images;
5. submit `XrCompositionLayerProjection`;
6. verify the simulator Graphics panel reports projection layers and advancing FPS.

Only after that gate passes should Viro scene rendering be enabled.

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
- Meta VR Glasses profile renders the compositor smoke test with no black frame.
- Minimal Viro scene renders in both eyes.
- Head pose updates view matrices.
- Restart after changing simulator device profile works.
- Projection layer is visible in the simulator Graphics panel.
- No GL/GLES graphics extension is requested on the desktop simulator path.
- Existing physical Quest/Horizon GLES path remains green.
- Passthrough-off immersive scene works before MR/passthrough is enabled.
- MR/passthrough failure degrades visibly and logs the capability/format failure rather than returning an unexplained black frame.

## Non-goals

Do not solve this with CSS/DOM/WebGL fallbacks, screen capture, or a separate Unity/Unreal application. This is a native Viro/OpenXR backend correction.
