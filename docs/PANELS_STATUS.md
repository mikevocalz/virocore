# Compositor Panels — verified branch inventory

Companion: [Viro External #58](https://github.com/mikevocalz/viro-external/pull/58).

## Present in this draft (checked against GitHub branch)

- `android/sharedCode/src/main/cpp/VROOpenXRPanelCompositor.h/.cpp`: per-panel GLES swapchain and quad/cylinder composition; explicit dirty state, acquired/waited/released lifecycle and typed registration errors.
- `android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.h/.cpp`: renderer ownership, optional cylinder extension, available layer budget, frame-period wait timeout, producer invalidation method.
- `android/sharedCode/CMakeLists.txt`: includes compositor implementation.
- `docs/OPENXR_PANEL_COMPOSITOR.md`: native contract and limitations.

**Partial Phase 1 work only:** source changes committed; no claim of successful NDK/device verification. Android Surface, input and RN host are not implemented here.

## Blockers that are not solved by a compiling GLES compositor

1. `XR_KHR_android_surface_swapchain` backend must be a separate panel kind; never call acquire/wait/release on its Surface swapchain.
2. Host must post real content from React Native into the Android `Surface` via a native display/presentation root. No dummy texture producer is acceptable.
3. The SDK-free C++ fake image-state test now covers wait timeout, redraw failure, retrying release and first-frame failure (four scenarios passed locally). Still missing: mocked OpenXR API boundary, resource teardown, STOPPING and resumed-session tests.
4. The native host must use the existing XR renderer queue; no additional `XrSession`.
5. Native input, occlusion policy, native layer eviction and actual scene fallback remain unwired.
6. Quest 3 and PICO 4 Ultra device evidence remains required: curved/flat alpha, rays, drag release, pause/resume, resource counts, frame times.

## Merge policy

Keep #111 draft until the NDK build and both headset tests are verified. Do not treat host designs, CI queued, or a registered producer callback as proof that React Native UI is visible in the compositor.
