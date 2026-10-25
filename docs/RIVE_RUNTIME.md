# rive-runtime in libviro_renderer.so

ViroCore links Rive's C++ runtime and its GLES renderer statically into
`libviro_renderer.so` on Android arm64-v8a. This is the build step from
viro-external ADR 0001, decision D3. No Rive producer exists yet: the library is linked,
and a link-check table keeps the import and GL render-context paths in the binary,
but nothing calls them.

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
- `android/sharedCode/src/main/cpp/VRORiveRuntimeProbe.cpp` is the link check.

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
   the probe, because `viro_renderer` itself builds with default visibility and
   `--exclude-libs` only covers archive members.

`viro_rive_sources()` also applies the defines and flags the archives were built with:
`RIVE_ANDROID WITH_RIVE_TEXT WITH_RIVE_LAYOUT RELEASE NDEBUG YOGA_EXPORT=`, `-std=c++17
-fno-rtti -fno-exceptions -UDEBUG`, and the force-included `rive_yoga_renames.h` and
`rive_harfbuzz_renames.h`. Rive's headers add members under `DEBUG`, so a TU that saw
different defines would disagree with the archives about class layout. Any Rive producer
added later must be listed in `viro_rive_sources()`.

Check on the AAR (`llvm-nm` from the NDK). `grep -i rive` alone also matches ViroCore's
own `Driver` and `derive` symbols, so they are filtered out:

```sh
nm -D jni/arm64-v8a/libviro_renderer.so | grep -i rive | grep -vi driver | grep -v derive
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

This is the size with the probe's two entry points retained: `rive::File::import` (the
whole object model, text shaping and layout) and
`rive::gpu::RenderContextGLImpl::MakeContext` (the GLES renderer and its embedded
shaders). A real producer will reach roughly the same code. The armeabi-v7a `.so`
(5,492,392 bytes) has no `rive::` symbols; the supplied base has no armeabi-v7a build to
compare against.

The base is the `.so` supplied with the task rather than one rebuilt from this branch
with `-DVIRO_WITH_RIVE=OFF`, so the delta assumes that build matches `70e95fd8`.

## Updating the pin

1. Replace `ViroRenderer/rive-runtime/` with the new commit, minus the paths listed in
   its `NOTICE.md`, and re-apply the patch.
2. Write the new hash into `android/rive/RIVE_RUNTIME_COMMIT` and both NOTICE files, and
   update the dependency tags and commits in `ViroRenderer/rive-runtime/NOTICE.md`.
3. Re-check whether text still needs layout; if not, and the app's `.riv` files still
   have no child `LayoutComponent`s, drop `--with_rive_layout` from the script and
   `WITH_RIVE_LAYOUT` from `ViroRive.cmake` together.
4. Build, then re-run the alignment, `nm -D` and size checks above.
