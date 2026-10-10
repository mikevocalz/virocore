# OpenXR native compositor-backed panels (Android GLES)

This is the **engine-side** implementation. It is not yet a React Native
public component. The `VROSceneRendererOpenXR` session is the sole owner of
panel swapchains; do not create a second OpenXR session from Viro External.

## Supported now, in source
- Native, independent `xrCreateSwapchain` per **registered** panel.
- Compositor `XrCompositionLayerQuad` and capability-gated
  `XrCompositionLayerCylinderKHR` (only after actually enabling
  `XR_KHR_composition_layer_cylinder` at instance creation).
- Source texture is produced into the XR swapchain image **on the render
  thread**, through `VROOpenXRPanelProducer`. Registration fails without a
  producer. A producer returning false or failing to draw is not submitted.
- Acquired image always released, even on wait/producer failure.
- Stable panel ID, position/rotation updates and visibility, explicit
  remove/teardown before session destruction.
- Per-frame compositor layer-budget enforcement from
  `XrSystemProperties.graphicsProperties.maxLayerCount`, including
  projection and passthrough layers. Registering a panel does not guarantee
  it will fit on every frame; rendering fallback remains the caller's job.

## Integration contract (not yet wired)
1. A Viro-renderer-native texture producer must supply actual panel content
   (e.g. GPU blit or Skia/WebGPU surface drawn to the XR texture). No stand-in
   pixels or dummy composition layers are generated. Producer must preserve
   its own GL state and submit its draw before returning success.
2. React Native/Fabric needs an imperative presenter/host for registration,
   pose, visibility, resize and release. It must dispatch operations to the
   existing `VROPlatformDispatchAsyncRenderer` queue, never touch the
   session from the JS thread.
3. Viro External's `SpatialPanel` should make `scene` the fallback until
   native host capabilities **and** producer/interaction registration are ready.
4. For compositor-hit-test, map controller/ray/hand input into the same
   world pose and quad/cylinder surface; pointer capture and true drag end
   need the Viro input source to expose release, including loss of hover.
5. Meta `SpatialWindow` and PICO `WindowContainer` are separate OS-managed
   presentation paths, not OpenXR compositor layers.

## Device validation gates
- Android NDK OpenXR build and headset test (Quest and PICO).
- Acquire/wait/release balanced across failures and every lifecycle transition.
- No previous-frame stale imagery on return-false.
- Alpha blending, sRGB correctness and z-order vs passthrough.
- Native producer actual text/media, occlusion and input behavior.
- Cylindrical projection geometry, UV mapping, rays.
- Metal/Vulkan backends require their own texture-image descriptors.

This PR cannot be declared production-ready merely because C++ compiles.
