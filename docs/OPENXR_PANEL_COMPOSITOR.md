# OpenXR native compositor-backed panels (Android GLES)

This is the engine side of native panels, paired with [Viro External #58](https://github.com/mikevocalz/viro-external/pull/58). It does not make React Native views compositor layers yet. `VROSceneRendererOpenXR` owns the existing `XrSession`; never create another session from the JS host.

## Current engine contract

- One independent `xrCreateSwapchain` for each registered GLES texture producer. A valid producer is required.
- Native `XrCompositionLayerQuad` and capability-gated `XrCompositionLayerCylinderKHR`; the latter requires `XR_KHR_composition_layer_cylinder` at instance creation.
- Panel validation returns `VROOpenXRPanelError` for duplicate or empty IDs, absent session/producer, layer budget, invalid geometry, unavailable cylinder, and allocation failures. `VROSceneRendererOpenXR::addCompositorPanel` passes the same error through, and returns `SessionUnavailable` before the session exists or after teardown.
- `addPanel` makes content dirty initially. After one successful acquire → wait → producer draw → release, later frames **reuse the last released image** until `invalidatePanelContent(id)` or `invalidateCompositorPanelContent(id)` requests fresh pixels. Pose changes and visibility do not repaint texture content.
- **Wait must return `XR_SUCCESS`** before an image can be released; `XR_TIMEOUT_EXPIRED` is NOT a successful wait. A timed-out or failed wait retains the acquired image for another frame rather than releasing it illegally.
- If drawing fails after a successful wait, retain that acquired/waited image for redraw and continue submitting the last released frame. If `xrReleaseSwapchainImage` fails, retry release without drawing again. No panel is submitted before its first successful release.
- Content invalidation during a producer draw or a release retry survives that release and schedules another draw.
- The wait timeout is the predicted display period from `XrFrameState`, not infinite.
- `BLEND_TEXTURE_SOURCE_ALPHA_BIT` is set without `UNPREMULTIPLIED_ALPHA_BIT`: texture producers **must draw premultiplied alpha**.
- The panel budget is the runtime `maxLayerCount` minus two slots held for projection and passthrough, so `addPanel` rejects with `LayerBudgetExceeded` instead of admitting a panel that never composites. Each frame also recomputes the free slots. Capacity is not a guarantee of successful per-frame composition; the host must implement a real scene-mesh fallback.
- A cylinder `pose` is the centre of the circle, not the panel surface (OpenXR `XrCompositionLayerCylinderKHR`). The visible arc sits `radiusMeters` in front of it.
- Producer GPU work is completed before destroying panel swapchains. The render thread also drains it before releasing its GLES context, so later session teardown does not call GL without a current context.
- Call compositor methods on the existing OpenXR render thread. Destroy panel resources before the parent session is destroyed.

## Important limits

The producer callback remains GLES texture-only and is not wired to RN/Fabric/Expo. The attached design pack recommends a **separate** `XR_KHR_android_surface_swapchain` kind and Android `VirtualDisplay`/`Presentation` host rather than pretending a Surface swapchain can use acquire/wait/release. Do not mix the lifecycle rules of those swapchain kinds.

The renderer has no automatic dirty notification from the eventual React root. Dynamic content needs a host-side invalidation call. Pending acquired-but-not-waited images during session teardown also require explicit device/runtime verification; correctness during STOPPING and session loss is not yet demonstrated.

## Integration checklist

- [ ] Runtime-gated Android Surface panel creation (only `xrDestroySwapchain` on those swapchains).
- [ ] Expo Modules 2.0 Android SharedObject host, JNI render-queue ownership, RN root to virtual display.
- [ ] Controller/hand ray to quad/cylinder pixel coordinates, reliable input capture and release.
- [ ] Native budget eviction callbacks and reliable `scene` fallback; depth testing when supported.
- [ ] Meta system windows and PICO WindowContainers validated separately in shared-space mode.
- [ ] Device-verified lifecycle, sRGB, alpha, passthrough ordering, geometry and performance on Quest 3 and PICO 4 Ultra.
- [x] SDK-free C++ state-machine regression: six scenarios (timeout, failed redraw, release retry, first-frame failure, invalidation during release retry, invalidation during draw) compiled and passed locally with `g++ -std=c++17 -Wall -Wextra -Werror`; CI workflow also added.
- [x] Android NDK compile: `VROOpenXRPanelCompositor.cpp` and `VROSceneRendererOpenXR.cpp` pass `-fsyntax-only` with the arm64-v8a NDK r27.1 flags from the cached viroreact `compile_commands.json`. Android debug and PICO release AAR builds also passed in PR CI.
- [ ] Mocked real OpenXR calls, STOPPING cleanup and session-transition/device tests.
- [ ] Metal/Vulkan image backend implementations (excluded from Android GLES v1).

Reference: [Khronos rendering chapter](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc), [cylinder extension](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_composition_layer_cylinder.adoc), [Android Surface extension](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_android_surface_swapchain.adoc).

## Merge policy

The engine API merges with nothing calling it. No code registers a panel, so on `main` the only runtime changes are an optional extension request, one `xrGetSystemProperties` call and an empty compositor per session. Headset evidence on Quest 3 and PICO 4 Ultra gates the first host that calls `addCompositorPanel`, not this API.