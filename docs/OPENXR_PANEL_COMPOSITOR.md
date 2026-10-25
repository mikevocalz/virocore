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
- The panel budget is the runtime `maxLayerCount` minus two slots held for projection and passthrough, so `addPanel` rejects with `LayerBudgetExceeded` instead of admitting a panel that never composites. Each frame also recomputes the free slots. Capacity is not a guarantee of successful per-frame composition, so a visible panel that gets no slot is no longer dropped silently: it becomes `Evicted` and the presentation listener hears it (see [Eviction and the scene fallback](#eviction-and-the-scene-fallback)).
- A cylinder `pose` is the centre of the circle, not the panel surface (OpenXR `XrCompositionLayerCylinderKHR`). The visible arc sits `radiusMeters` in front of it.
- Producer GPU work is completed before destroying panel swapchains. The render thread also drains it before releasing its GLES context, so later session teardown does not call GL without a current context.
- Call compositor methods on the existing OpenXR render thread. Destroy panel resources before the parent session is destroyed.

## Eviction and the scene fallback

Each visible panel has a presentation: `Pending` until it first gets a slot or is first refused one, then `Compositor` or `Evicted`. `appendLayers` hands out slots in registration order. A panel that has never released an image holds no slot and stays `Pending`. Changes are reported after the layer list is built, with a reason:

- `LayerBudget`: no free slot this frame.
- `LayerLimitExceeded`: `xrEndFrame` returned `XR_ERROR_LAYER_LIMIT_EXCEEDED`, so the renderer now caps layers one below the count the runtime rejected. That frame is lost. The cap is cleared when a panel is removed.

An evicted panel's producer is not called. Invalidations still mark it dirty, so it redraws when it returns to `Compositor`, which happens without host action once a slot frees.

`VROOpenXRPanelCompositor::setPresentationListener` runs inside `appendLayers`, before `xrEndFrame`, while the layer list points into the compositor. It must not add, remove or update panels. `VROSceneRendererOpenXR::setCompositorPanelPresentationListener` is the host-facing version: it is called at the start of the next frame, where the host may change panels and the scene graph.

For Rive panels (`addRiveCompositorPanel`) the renderer builds `VROOpenXRRivePanelFallback` on first eviction. It is a mipmapped `VROTexture` filled from the same Rive ring with the same copy program and coverage function as the finish pass, refreshed on every published Rive frame while the panel is evicted. `getRiveCompositorPanelFallbackTexture(id)` returns it. Viro's scene graph belongs to the host, so the renderer does not create the fallback node itself. The Java API below hands the texture to the host, and viro-external `@viro-external/spatial-panels` puts it on a quad (or cylinder arc) of the panel's pose and size with a `PremultiplyAlpha` material, shows it while the panel is `Evicted` and hides it on `Compositor`.

## Java API

`com.viro.core.RiveCompositorPanel` (JNI in `android/sharedCode/src/main/cpp/jni/RiveCompositorPanel_JNI.cpp`):

| Call | Thread it runs on |
| --- | --- |
| `getCapabilities(view, callback)`: session ready, cylinder, panel budget, Rive linked | XR render thread; answer on the main thread |
| `create(view, config, listener, callback)`: `addRiveCompositorPanel`, then artboard, state machine and `.riv` | XR render thread; `onCreated` or `onFailed(CreateError, message)` on the main thread |
| `setPose`, `setVisible`, `dispose` | Posted to the XR render thread |
| `setNumber`, `setBoolean`, `setString`, `fireTrigger`, `pointer`, `setActive` | Queued to the Rive worker from the calling thread |
| `Listener.onPresentationChanged`, `Listener.onStatus` | Main thread |

Render-thread work waits while the render loop is paused. The renderer keeps one host presentation listener, so the JNI layer keeps a per-renderer registry of panels (render thread only) and dispatches by id.

Fallback texture: on the first `Evicted` change the JNI layer wraps the renderer's fallback in one `com.viro.core.Texture` and Java caches it (`getFallbackTexture()`); later evictions reuse it. That texture holds a reference to the `VRORivePanel`, so the host disposes it after `dispose()`.

Teardown: `dispose()` posts `removeCompositorPanel(id)` (swapchain first, then the finish pass), then `VRORivePanel::dispose()` on a background thread so joining the worker does not stall a frame. The ring and the shared context therefore still go after the swapchain. The Java object is held through a weak global ref that only the main thread reads and deletes, so callbacks queued after `dispose()` find it gone. A session teardown removes the layers without a Java callback.

JNI integer codes are in `RiveCompositorPanelMapping.h`; `scripts/test-compositor-panel-mapping.sh` checks them against the Java enum order.

## Rive finish pass, the first producer

`VROOpenXRRivePanelProducer` (viro-external ADR 0001 route B2). `addRiveCompositorPanel(desc, cornerRadiusMeters, options, listener, &panel)` creates a `VRORivePanel` in texture-ring mode and registers the producer for it.

| | |
| --- | --- |
| Rive side | Worker thread, GLES 3 context created with this session's `_eglContext` as share context, 1x1 pbuffer. Renders into three immutable `GL_RGBA8` textures (`VRORivePanelRing`) through `TextureRenderTargetGL`, rows bottom-up |
| Producer ready | Worker: `glFenceSync` + `glFlush` after each frame, then publish. XR thread: `glWaitSync` on that fence in `beginFrameRead` (GPU wait; the CPU never blocks) |
| Consumer complete | XR thread: fence + flush after the copy in `endFrameRead`. Worker never writes the newest slot or the one being read, and `glWaitSync`s on a slot's consumer fence before rendering into it |
| Invalidation | Each published Rive frame posts one `invalidatePanelContent(id)` to the XR thread through the direct renderer queue (at most one in flight per panel). Rive publishes only while `advanceAndApply` returns true or after input, data or lifecycle changes |
| First frame | Until Rive has published one, the producer returns false and the compositor keeps the acquired image; no layer is submitted |

Colour and alpha. Rive writes premultiplied, sRGB-encoded bytes into UNORM textures. For a `GL_SRGB8_ALPHA8` swapchain the copy computes `s = min(C / a, 1)`, `linear = decode(s)`, writes `(linear * a, a)`, and the hardware encodes on write; the runtime reads back premultiplied linear, which is what `BLEND_TEXTURE_SOURCE_ALPHA_BIT` without `UNPREMULTIPLIED_ALPHA_BIT` requires. Copying the bytes straight would encode twice, and decoding the premultiplied byte directly would give `decode(a * s)`, darker than `a * decode(s)` on every partially covered pixel. For a `GL_RGBA8` swapchain the premultiplied encoded bytes pass through, the same convention the eye buffers use in that mode. All four channels are then multiplied by `VROPanelCoverage` (`ViroRenderer/VROPanelCoverage.h`) with the corner radius converted per axis. OpenXR quad and cylinder layers have no corner field, so the corners are transparent premultiplied pixels; on a cylinder they are in texture space and follow the arc.

GL state. The pass runs on Viro's context between Viro's frames and uses `VROPanelGLStateGuard`: draw and read framebuffer, viewport, program, vertex array, array buffer, active texture, unit 0's 2D and external bindings and sampler, colour mask, and the blend, depth test, cull, scissor and stencil enables, plus `GL_FRAMEBUFFER_SRGB_EXT` when `EXT_sRGB_write_control` exists. Everything it changes is restored to its prior value. `VRODriverOpenGL::invalidateGLStateCache()` would forget only textures, render target and shader, and would leave depth, colour mask, stencil, cull and blend cached, so restoring is the only way to keep the whole cache true; the pass does not call it. The swapchain image is detached from the pass's framebuffer after each draw.

Teardown order. `removeCompositorPanel(id)`: `glFinish`, `xrDestroySwapchain`, then the producer callback is dropped and the producer deletes its program, framebuffer and buffers on the XR context. Then it releases the Rive panel. When that was the last reference, `dispose()` joins the worker, which waits for any read in progress and its fence, then deletes the ring textures and its shared context. The ring and the shared context therefore always outlive the swapchain. At session teardown the XR context is no longer current, so the producer skips its GL deletes and those objects go with the context, which is destroyed after `_panelCompositor`.

## Important limits

The producer callback remains GLES texture-only and is not wired to RN/Fabric/Expo. The attached design pack recommends a **separate** `XR_KHR_android_surface_swapchain` kind and Android `VirtualDisplay`/`Presentation` host rather than pretending a Surface swapchain can use acquire/wait/release. Do not mix the lifecycle rules of those swapchain kinds.

The renderer has no automatic dirty notification from the eventual React root. Dynamic content needs a host-side invalidation call. Pending acquired-but-not-waited images during session teardown also require explicit device/runtime verification; correctness during STOPPING and session loss is not yet demonstrated.

## Integration checklist

- [ ] Runtime-gated Android Surface panel creation (only `xrDestroySwapchain` on those swapchains).
- [x] JNI render-queue ownership and a Java API for Rive compositor panels (`RiveCompositorPanel`, above). Release AAR builds, 16KB alignment passes, mapping host test passes; not run on a headset.
- [x] Expo Modules 2.0 Android SharedObject host for Rive panels: viro-external `@viro-external/spatial-panels`. Kotlin compiled with kotlinc only; no app build, no headset.
- [ ] RN root to virtual display (Android Surface swapchain kind).
- [ ] Controller/hand ray to quad/cylinder pixel coordinates, reliable input capture and release.
- [x] Native eviction reporting: per-panel presentation with reason (layer budget, `XR_ERROR_LAYER_LIMIT_EXCEEDED` cap), forwarded to the host at the next frame. Host-tested against a fake runtime (`scripts/test-openxr-panel-eviction.sh`, 30 checks, in the CI workflow); not yet run on a headset.
- [ ] Reliable `scene` fallback. Done in source for Rive panels: the renderer keeps the fallback texture current while evicted, the Java API hands it to the host, and `@viro-external/spatial-panels` binds it to a `PremultiplyAlpha` quad or arc and toggles it on presentation changes (node tests with a fake native module). Not done: any device run (orientation, pose agreement with the layer, no visible change on swap), non-Rive producers, depth testing when supported.
- [x] First real producer: the Rive finish pass, with the shared coverage function and the sRGB/premultiplied conversion above. Compiles in the arm64-v8a release AAR; not yet run on a headset.
- [ ] Meta system windows and PICO WindowContainers validated separately in shared-space mode.
- [ ] Device-verified lifecycle, sRGB, alpha, passthrough ordering, geometry and performance on Quest 3 and PICO 4 Ultra. For the Rive panel: the mid-grey and 50 % alpha swatch gate with no corner halo on both swapchain formats, upright orientation, cylinder corners at grazing angles, EGL context sharing accepted by both runtimes, zero producer calls over 10 s with a settled state machine, and an eviction that swaps to the fallback quad with no visible change.
- [x] SDK-free C++ state-machine regression: six scenarios (timeout, failed redraw, release retry, first-frame failure, invalidation during release retry, invalidation during draw) compiled and passed locally with `g++ -std=c++17 -Wall -Wextra -Werror`; CI workflow also added.
- [x] Android NDK compile: `VROOpenXRPanelCompositor.cpp` and `VROSceneRendererOpenXR.cpp` pass `-fsyntax-only` with the arm64-v8a NDK r27.1 flags from the cached viroreact `compile_commands.json`. Android debug and PICO release AAR builds also passed in PR CI.
- [ ] Mocked real OpenXR calls, STOPPING cleanup and session-transition/device tests.
- [ ] Metal/Vulkan image backend implementations (excluded from Android GLES v1).

Reference: [Khronos rendering chapter](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc), [cylinder extension](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_composition_layer_cylinder.adoc), [Android Surface extension](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_android_surface_swapchain.adoc).

## Merge policy

The engine API merges with nothing calling it. No code registers a panel, so on `main` the only runtime changes are an optional extension request, one `xrGetSystemProperties` call and an empty compositor per session. Headset evidence on Quest 3 and PICO 4 Ultra gates the first host that calls `addCompositorPanel`, not this API.