# PICO renderer integration notes

PICO runs inside Viro's native OpenXR renderer and shares session and scene
code with Quest. Expo-PICO supplies the PICO Android flavors and launcher
metadata; it does not replace the renderer.

## Plane capability status

`ViroViewOpenXR.getPlaneDetectionStatus()` (and `Renderer.getPlaneDetectionStatus()`)
returns:

- `-1` before the OpenXR session is created, and again after it is destroyed
- `0` when the session started but no plane source initialized
- `1` when a plane source initialized

The value lives in an atomic on `VROSceneRendererOpenXR`, so the Java UI thread
can read it without touching the GL thread. It leaves Expo-PICO's existing
`String[10]` runtime probe contract unchanged. It reports initialization only:
`1` does not promise that planes exist or that an asynchronous scan will
succeed.

## Building the PICO AAR

`./scripts/build-pico-aar.sh --out /absolute/artifacts` builds
`:viroreact:assembleRelease`, then runs `scripts/verify-16kb-alignment.py` on
the arm64-v8a libraries. The NDK pin is read from `android/viroreact/build.gradle`.
APK ZIP alignment is a separate check after app packaging.
