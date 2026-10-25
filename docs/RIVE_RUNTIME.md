# rive-runtime in libviro_renderer.so

ViroCore links Rive's C++ runtime and its GLES renderer statically into
`libviro_renderer.so` on Android arm64-v8a. This is the build step from
viro-external ADR 0001, decision D3. `VRORivePanel` is the code that uses it: the
scene-panel producer (route B1) and, in texture-ring mode, the source for the OpenXR
compositor finish pass (route B2); see [VRORivePanel](#vrorivepanel) and
[Texture-ring mode](#texture-ring-mode-route-b2) below.

| | |
| --- | --- |
| Upstream | https://github.com/rive-app/rive-runtime, MIT |
| Pinned commit | `8b3ca41b12bfd8ac9454444102c1fb130b3e7e11` (`android/rive/RIVE_RUNTIME_COMMIT`) |
| Source | `ViroRenderer/rive-runtime/` (provenance and local change in its `NOTICE.md`) |
| Local patch | `ViroRenderer/rive-runtime-patches/0001-accept-ndk-revision-override.patch` |
| NDK | 27.1.12297006, the `ndkVersion` in `android/viroreact/build.gradle` |
| ABI | arm64-v8a only. armeabi-v7a builds without Rive (`VRO_HAS_RIVE_RUNTIME=0`) |
| CMake switch | `VIRO_WITH_RIVE`, default ON |
| Licences | `NOTICE` at the repo root |

## Files

- `scripts/build-rive-runtime.sh` runs rive-runtime's own `build/build_rive.sh`
  (premake + ninja) for `android arm64 release`.
- `android/rive/premake5.lua` is the premake entry it runs. It adds
  `-fvisibility=hidden -fvisibility-inlines-hidden` to every rive project and builds the
  same projects as upstream's `renderer/premake5.lua`, without the `path_fiddle` sample.
- `android/rive/ViroRive.cmake`, included from `android/sharedCode/CMakeLists.txt`,
  imports the archives, links them into `viro_renderer` with `--exclude-libs`, and
  defines `viro_rive_sources()` for any source that includes rive headers.
- `android/sharedCode/src/main/cpp/VRORivePanel*.{h,cpp}` is the producer.
  `VRORivePanelScene.cpp` is the only one that includes rive headers.

Outputs go to `android/rive/out/android_arm64_release/` and fetched sources to
`android/rive/dependencies/`. Both are gitignored.

## Build recipe

```sh
scripts/build-rive-runtime.sh            # once, or after changing the pin
cd android && ./gradlew :viroreact:assembleRelease --no-daemon
```

The first step is optional. CMake configure runs the script itself (with `clean`) when
an archive is missing or `viro_rive.stamp` names a different commit. The first run
needs network access: `build_rive.sh` clones premake-core `v5.0.0-beta7` and builds it,
clones premake-ninja, and premake fetches HarfBuzz, SheenBidi, Yoga, miniaudio and ply at
the tags rive-runtime pins (listed in `ViroRenderer/rive-runtime/NOTICE.md`). Offline,
or to skip Rive, configure with `-DVIRO_WITH_RIVE=OFF`.

The script sets:

- `ANDROID_NDK` to the Gradle `ndkVersion` under the SDK,
- `RIVE_NDK_REVISION_OVERRIDE` to the same revision. Upstream accepts only r27c
  (27.2.12479018); the local patch lets it take r27b, so the archives and the rest of
  the `.so` share one toolchain,
- `RIVE_PREMAKE_ARGS="--with_rive_text --with_rive_layout --no-rive-decoders --android_api=24"`.

Release builds use upstream's defaults: `-O2`, LTO (`-flto`, so the archives hold LLVM
bitcode that lld compiles at the final link), no RTTI, no exceptions, C++17.

Archives at the pin (unstripped, bitcode): `librive.a` 28.5 MB, `librive_harfbuzz.a`
3.1 MB, `librive_pls_renderer.a` 1.6 MB, `librive_yoga.a` 0.5 MB,
`librive_sheenbidi.a` 0.1 MB.

## Modules

Checked against the three files in `MoyoLearn/apps/mobile/assets/rive/`
(`moyo_board_chrome.riv`, `moyo_fractions.riv`, `moyo_learning_question.riv`, all
format 7.3) by walking every object record with the type keys from
`include/rive/generated/`:

| Module | In the build | Why |
| --- | --- | --- |
| Text (HarfBuzz, SheenBidi) | on | Each file has 29 to 45 `Text` objects with `TextValueRun`s and one embedded `FontAsset`. |
| Layout (Yoga) | on | The files need little of it: each has one `LayoutComponentStyle` (the artboard's own) and no child `LayoutComponent`. It is on because text does not compile without it at this commit: `src/text/text_input.cpp:444` calls `LayoutComponent::hugsContentWidth()`, which `layout_component.hpp` declares only under `WITH_RIVE_LAYOUT`. The text-only build failed with exactly that error. |
| Audio (miniaudio) | off | No `AudioAsset` or `AudioEvent` objects. miniaudio headers are still on the include path because public rive headers include them. |
| Image decoders (`--no-rive-decoders`) | off | No `ImageAsset` objects. A future `.riv` with embedded images will fail to decode them until this is turned on, which adds libpng, libjpeg, libwebp and zlib. |
| Canvas (ORE), scripting (Luau), Vulkan | off | Not used by these files or by the GLES producer routes in the ADR. |

All three files also use view models and data binding (`ViewModel*`,
`DataBindContext`), which are part of the core runtime and need no option.

## Symbol visibility

Three layers keep rive out of the dynamic symbol table, so an app that also ships
`librive-android.so` (through `@rive-app/react-native`) cannot bind to ViroCore's copy or
the other way round:

1. Every rive archive is compiled with `-fvisibility=hidden -fvisibility-inlines-hidden`.
2. `viro_renderer` links with
   `-Wl,--exclude-libs,librive_pls_renderer.a:librive.a:librive_harfbuzz.a:librive_sheenbidi.a:librive_yoga.a`.
   The list is colon-separated because `-Wl,` splits on commas.
3. ViroCore sources that include rive headers go through `viro_rive_sources()`, which
   adds hidden visibility for that TU. Without it the first build exported one symbol,
   the inline `rive::File::import(..., FileAssetLoader*, ...)` overload instantiated in
   the former link-check probe, because `viro_renderer` itself builds with default visibility and
   `--exclude-libs` only covers archive members.

`viro_rive_sources()` also applies the defines and flags the archives were built with:
`RIVE_ANDROID WITH_RIVE_TEXT WITH_RIVE_LAYOUT RELEASE NDEBUG YOGA_EXPORT=`, `-std=c++17
-fno-rtti -fno-exceptions -UDEBUG`, and the force-included `rive_yoga_renames.h` and
`rive_harfbuzz_renames.h`. Rive's headers add members under `DEBUG`, so a TU that saw
different defines would disagree with the archives about class layout. Any Rive producer
added later must be listed in `viro_rive_sources()`.

Check on the AAR (`llvm-nm` from the NDK). `grep -i rive` alone also matches ViroCore's
own `Driver` and `derive` symbols and the exported `VRORivePanel` and
`*RiveCompositorPanel*` APIs, so they are filtered out:

```sh
nm -D jni/arm64-v8a/libviro_renderer.so | grep -i rive | grep -vi driver | grep -v derive | grep -v RivePanel
# (no output)
nm -D -C jni/arm64-v8a/libviro_renderer.so | grep -c 'rive::'          # 0
nm -D jni/arm64-v8a/libviro_renderer.so | grep -cE ' (rive_)?hb_'     # 0
nm -D -C jni/arm64-v8a/libviro_renderer.so | grep -cE 'YG|facebook::yoga'  # 0
```

The unstripped `.so` under `build/intermediates/cxx/` has 6,497 local `rive::` symbols,
so the code is linked; it just isn't exported.

Side effect: four libc++ template instantiations that the base `.so` exported as weak
symbols (`basic_string::__init_copy_ctor_external`,
`vector<string>::__push_back_slow_path`, and two `__hash_table<unsigned>` members) are
no longer exported, because the linker merged them with rive's hidden copies and kept
the stricter visibility. No other library in the AAR imports them; a consumer library
that did would get its own copy from the headers.

## Binary size

Stripped arm64-v8a `libviro_renderer.so` from `viroreact-release.aar`:

| | Bytes | `.text` | `.rodata` | `.eh_frame` |
| --- | ---: | ---: | ---: | ---: |
| Base (70e95fd8 build, supplied) | 7,568,144 | 4,177,272 | 494,988 | 735,568 |
| With rive-runtime | 10,547,328 | 6,234,796 | 680,588 | 1,021,504 |
| Delta | +2,979,184 (+2.84 MiB, +39.4%) | +2,057,524 | +185,600 | +285,936 |

This was measured with the former link-check probe, which retained two entry points: `rive::File::import` (the
whole object model, text shaping and layout) and
`rive::gpu::RenderContextGLImpl::MakeContext` (the GLES renderer and its embedded
shaders). A real producer will reach roughly the same code. The armeabi-v7a `.so`
(5,492,392 bytes) has no `rive::` symbols; the supplied base has no armeabi-v7a build to
compare against.

The base is the `.so` supplied with the task rather than one rebuilt from this branch
with `-DVIRO_WITH_RIVE=OFF`, so the delta assumes that build matches `70e95fd8`.

## VRORivePanel

Scene-panel producer for ADR 0001 route B1. The exported API is
`android/sharedCode/src/main/cpp/VRORivePanel.h`; it names no rive type, so the Nitro
HybridObjects in `@viro-external/native-panels` link `libviro_renderer.so` without seeing
rive. On armeabi-v7a, or with `-DVIRO_WITH_RIVE=OFF`, the same symbols exist,
`isAvailable()` is false and `create()` returns null.

| File | Role | Flags |
| --- | --- | --- |
| `VRORivePanel.cpp` | Facade, command queue, worker thread, state machine | C++17, exceptions |
| `VRORivePanelEGL.cpp` | EGL display/config/context, pbuffer, window surface, window ref | C++17, exceptions |
| `VRORivePanelGeometry.cpp` | Fit/alignment transform, corner hit test, pointer routing | C++17 |
| `VRORivePanelRing.cpp` | Texture ring for route B2: three RGBA8 textures and their fences | C++17 |
| `VRORivePanelScene.cpp` | Render context, File, artboard, state machine, view model | `viro_rive_sources()` |
| `VROOpenXRRivePanelProducer.cpp` | Route B2 finish pass on the XR render thread | C++14, all ABIs |
| `VROOpenXRRivePanelFallback.cpp` | Scene-mesh fallback texture for an evicted B2 panel | C++14, all ABIs |

### Ownership and threads

| Object | Owner | Created, used, destroyed on |
| --- | --- | --- |
| Command queue | `VRORivePanel::Impl` | any thread pushes (one mutex), worker drains |
| `EGLContext` (GLES 3, not shared with Viro), 1x1 pbuffer | worker | worker |
| `EGLSurface` on the ANativeWindow | worker | worker |
| ANativeWindow reference | worker (`ANativeWindow_acquire` on the caller thread, released on the worker after the EGL surface) | caller acquires, worker releases |
| `RenderContextGLImpl`, `FramebufferRenderTargetGL`, `File`, `ArtboardInstance`, `StateMachineInstance`, `ViewModelInstanceRuntime` | worker | worker, context current |
| Surface buffer queue | Android BufferQueue | producer: worker via `eglSwapBuffers`; consumer: Viro's SurfaceTexture |

Hand-off: the worker renders into the window surface and calls `eglSwapBuffers`, which
queues the buffer; Viro latches it with `updateTexImage`. No texture or context is shared,
so no fence crosses between the two. Swap interval is 0, which puts the producer in async
mode: an unlatched frame is replaced by the next one and the worker never blocks on Viro.
Frame pacing comes from the worker: it sleeps on a condition variable, untimed while the
state machine is settled and until `last frame + 1 / maxFrameRateHz` while it animates.
Elapsed time per advance is clamped to 0.1 s, and the first advance after a settled frame
uses 0.

A frame is drawn only when `advanceAndApply` returned true on the previous frame, or after
a pointer event, view-model write, load or selection, resize, surface attach or
`setActive(true)`. Commands queued since the last wake are applied together before that
one advance.

### Pixels

- Target: `FramebufferRenderTargetGL` on the window's FBO 0. With `EXT_shader_pixel_local_storage` Rive draws into FBO 0 directly; without it,
  Rive renders to an internal texture and blits back. The alternative,
  `TextureRenderTargetGL` on a texture the panel owns plus one blit, always pays the blit but
  could also feed the B2 compositor ring. Which is cheaper has to be measured on Quest 3 and
  PICO 4 Ultra.
- Format: RGBA8 config with exactly 8 bits per channel, depth 24 and stencil 8 when the
  device offers them, window buffers `WINDOW_FORMAT_RGBA_8888` at the panel size.
- Colour: the window surface is created with `EGL_GL_COLORSPACE_LINEAR_KHR` (EGL's default,
  stated explicitly when the extension exists). Rive's GL backend writes sRGB-encoded bytes
  into UNORM targets and the scene route samples the Surface as sRGB-encoded bytes (D6), so
  nothing re-encodes them. An sRGB window surface would encode twice.
- Alpha: cleared to transparent, and Rive's output is premultiplied. The material that
  samples this Surface must use `PremultiplyAlpha` blending, and the `ExternalSurfaceTexture`
  must be told the input is premultiplied (`setPremultipliedAlpha(true)`) so the resolve
  copy keeps it exact on sRGB storage (see `VROAndroidViewTexture.h`).
- Corners: drawn square. The consumer rounds them with the shared coverage function
  (`ViroRenderer/VROPanelCoverage.h`): `ExternalSurfaceTexture.setCornerRadius(radiusM,
  widthM, heightM)` in the scene route, the finish pass in route B2 (ADR 0001 "Corner
  ownership"). `cornerRadiusPx` makes Rive clip its own corners with `clipPath`; it defaults
  to 0 and is only for displays that do not round at the consumer, since with Viro it would
  round twice.
- Rows: `FramebufferRenderTargetGL::bottomUp()` is true (`render_target_gl.hpp:207`), so
  Rive draws in the usual GL window orientation. Not yet checked through Viro's
  SurfaceTexture sampling; see the device list.

### Input

`pointerDown/Move/Up` take panel pixels (origin top-left). The worker maps them to
artboard space with the inverse of `VRORiveComputeAlignment`, the same matrix the frame is
drawn with, so a hit lands where the pixel was drawn. The corner test is
`VROPanelCoverage >= 0.5` with the consumer's per-axis radii (`coverageRadiusXPx/YPx`,
set from `VROPanelCornerRadiiPx`; `addRiveCompositorPanel` fills them in), falling back to
`cornerRadiusPx`. A press that starts in a transparent corner is dropped; a press that started inside keeps its moves and release even in a
corner. Hover that leaves the shape becomes one `pointerExit`.
`scripts/test-rive-panel-geometry.sh` checks the transform is bit-identical to
`rive::computeAlignment` (linked from the vendored source) and covers the corner test,
the per-axis radii and routing. `scripts/test-panel-coverage.sh` tests the coverage function
itself and compiles its GLSL with the NDK's glslc.

### Lifecycle

States: `initializing` → `waitingForSurface` → `active` / `suspended`, plus `lost` and the
terminal `disposed`. `surfaceLost()` blocks until the EGL surface is destroyed and the window
reference released, then returns; File, artboard and state machine stay. A swap error that
means the Surface was abandoned (`EGL_BAD_SURFACE`, `EGL_BAD_NATIVE_WINDOW`) drops the
window and reports `lost`; context loss, a render error or an exception tears down every
GPU object and reports `lost`. `attachSurface()` recovers from either, re-importing the last
file with its artboard, state machine and the view-model values written since it loaded.
`dispose()` is idempotent and joins the worker; from inside the status listener it returns
at once and the worker finishes teardown after the listener returns. `eglTerminate` is never
called: the display is process-wide and Viro uses it.

### Texture-ring mode (route B2)

`VRORivePanel::createTextureRing(display, shareContext, w, h, options, listener,
frameListener)`; normally called through `VROSceneRendererOpenXR::addRiveCompositorPanel`.

| Object | Owner | Created, used, destroyed on |
| --- | --- | --- |
| `EGLContext` (GLES 3, shares the XR render context), 1x1 pbuffer | worker | worker |
| Three immutable `GL_RGBA8` ring textures, producer and consumer fences | worker | created and deleted on the worker; sampled and fenced on the XR thread |
| Ring slot table (newest, being read, fences) | `VRORivePanelRing` | one mutex, no GL call under it |
| `TextureRenderTargetGL` on the current slot | worker | worker |
| Finish-pass program, framebuffer, vertex array and buffer | `VROOpenXRRivePanelProducer` | XR thread, XR context |

- Frames are rendered under the same rules as the window mode: only while
  `advanceAndApply` returns true or after input, data, load or lifecycle changes. After
  each one the worker fences and flushes, publishes the slot, and calls `FrameListener`,
  which posts one compositor invalidation to the XR thread.
- The XR thread reads with `beginFrameRead` (GPU wait on the producer fence) and
  `endFrameRead` (consumer fence). The worker never renders into the newest slot or the
  one being read, and GPU-waits on a slot's consumer fence before reusing it.
- Pixels: premultiplied, sRGB-encoded, square corners, rows bottom-up. The finish pass
  converts them for the swapchain format and applies the corners; see
  `docs/OPENXR_PANEL_COMPOSITOR.md`.
- No window: `attachSurface` and `surfaceLost` return false. `resize` rebuilds the ring.
  After a GPU failure the state is `lost` and `setActive(true)` rebuilds the context and
  ring and re-imports the file.
- Teardown: the ring and the shared context are destroyed by the worker in `dispose()`,
  which the finish pass triggers only after the compositor has destroyed the swapchain
  (or later, if the host still holds the panel).
- Eviction: when the compositor has no layer slot for the panel, the renderer fills
  `VROOpenXRRivePanelFallback` from the same ring for a scene quad. The host binding for
  that quad does not exist yet.

### Needs a device

None of the following has run yet. Each needs Quest 3 and PICO 4 Ultra:

- First frame of `moyo_board_chrome.riv` through CanvasSourceBridge, with the material in
  `PremultiplyAlpha`: mid-grey and 50 % alpha swatches over passthrough with no corner halo.
- Which interlock mode Rive picks on Adreno (log line from `RenderContextGLImpl`), and
  whether FBO 0 renders directly or through the offscreen blit.
- That a settled state machine produces zero swaps (count `eglSwapBuffers` over 10 s idle).
- `surfaceLost()` then `SurfaceTexture.release()` with no `EGL_BAD_NATIVE_WINDOW` in logcat;
  `attachSurface()` afterwards shows the same artboard state.
- The panel is upright, not mirrored vertically, in the scene.
- Pointer hits on BoardChrome buttons at the panel edges and corners with `contain` fit.
- Frame time per Rive frame, against the `TextureRenderTargetGL` + blit alternative.
- Route B2: that both runtimes accept a worker context sharing the XR context with a
  different config; first frame through `addRiveCompositorPanel` on an sRGB and an RGBA8
  swapchain with the swatch gate; upright orientation of the ring copy; finish-pass GPU
  time; no producer call while the state machine is settled; `removeCompositorPanel`
  followed by `dispose()` with no GL errors; behaviour across session STOPPING and loss.

## Updating the pin

1. Replace `ViroRenderer/rive-runtime/` with the new commit, minus the paths listed in
   its `NOTICE.md`, and re-apply the patch.
2. Write the new hash into `android/rive/RIVE_RUNTIME_COMMIT` and both NOTICE files, and
   update the dependency tags and commits in `ViroRenderer/rive-runtime/NOTICE.md`.
3. Re-check whether text still needs layout; if not, and the app's `.riv` files still
   have no child `LayoutComponent`s, drop `--with_rive_layout` from the script and
   `WITH_RIVE_LAYOUT` from `ViroRive.cmake` together.
4. Build, then re-run the alignment, `nm -D` and size checks above.
