# Eskiu Android / Quest / PICO enablement

This track closes the public Eskiu roadmap gap for Android before any production
Quest/PICO ViroCore module is migrated from C++.

## Why separate this from renderer migration

Eskiu v0.9.2 documents native AArch64 C ABI support, but its public roadmap still
lists these Android items as incomplete:

- `aarch64-linux-android` validation against the NDK/bionic;
- JNI shared-library integration;
- Gradle/NDK packaging.

Generic AArch64 success is therefore not enough to authorize a Quest/PICO backend swap.

## Staged acceptance gates

### A. Object emission
`eskiuc --target aarch64-linux-android -c` emits a valid AArch64 ELF relocatable object.

### B. NDK link
The object links with the selected NDK clang into a loadable `.so` without host-libc leakage.

### C. C++/JNI bridge
A normal Android C++ JNI wrapper calls an exported Eskiu C-ABI symbol and returns a deterministic result.

### D. Gradle packaging
The `.so` is produced/packaged by the existing ViroCore Gradle+CMake build for arm64-v8a.

### E. Android packaging constraints
- 16 KB page-size/alignment checks stay green;
- no unsupported text relocations;
- no unexpected runtime dependency on host-only libraries;
- release/strip symbols remain intentional.

### F. Hardware smoke
Run on an Android arm64 device before moving to Quest/PICO.

### G. Quest/PICO
Only after A-F:
- build inside the OpenXR renderer artifact;
- compare C++ and Eskiu paths behind a feature flag;
- measure memory, frame p95 and input latency;
- then consider one hot-path migration.

## Probe

```bash
ESKIUC=/path/to/eskiuc \
ANDROID_NDK_HOME=/path/to/ndk \
bash scripts/eskiu/probe-android-target.sh
```

The current probe covers gates A and B plus exported-symbol inspection. It intentionally
does not pretend JNI/Gradle/hardware validation is complete.
