# Eskiu Android target enablement

This track exists because Eskiu v0.9.2 documents generic AArch64 C ABI support but still lists Android NDK/JNI/Gradle support as unfinished.

## Probe stages

`scripts/eskiu/probe-android-ndk.sh` tests the smallest useful boundary:

1. locate pinned `eskiuc`;
2. locate the Android NDK;
3. attempt `--target aarch64-linux-android` object emission;
4. inspect the object;
5. link it with the NDK's Android clang.

The probe intentionally reports a distinct **BLOCKED** result when object generation is not supported. That is useful evidence, not a false CI failure.

## Subsequent milestones

After object emission/link succeeds:

- build an Eskiu object into a JNI-loadable shared library;
- call one Eskiu function through JNI;
- wire the object into ViroCore's CMake/Gradle graph;
- verify no C++ exception/runtime leakage across the ABI;
- verify Android 16 KB page-size/alignment requirements;
- run on physical Android hardware;
- run on Quest;
- run on PICO;
- only then allow an Eskiu-backed OpenXR hot path.

## Non-goal

This track does not migrate `VROSceneRendererOpenXR` or any production renderer class until the toolchain and device gates are green.
