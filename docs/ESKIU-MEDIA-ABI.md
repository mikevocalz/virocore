# Eskiu camera/media frame boundary

The media ABI supports two important paths without forcing either into the other:

1. **CPU-backed planes** such as RGBA/YUV camera frames, exposed as borrowed byte views.
2. **GPU-backed frames** exposed through the opaque surface handle already used by panel/video surfaces.

A frame may carry both when a platform exposes a GPU surface but a CPU consumer also
needs plane data.

## Lifetime

- `BORROWED_CALL`: plane pointers are valid only for the current call.
- `BORROWED_LEASE`: pointers live while a producer-owned opaque lease is valid.
- `BACKEND_OWNED`: the frame is represented primarily by an opaque surface.

This ABI does not expose CVPixelBuffer, AHardwareBuffer, Metal, OpenGL, WebGPU, or
MediaCodec object layouts.

## Copy accounting

The ABI cannot infer whether a platform converted or copied a frame. Adapters record
copy count and copied bytes explicitly. That gives the later C++/Eskiu A/B harness a
truthful baseline for camera texture and recording work.

## Planned adapters

- ViroCameraTexture
- video/external video surface path
- media recording/capture
- VisionCamera/Fishjam-style frame producers where Viro consumes their texture/frame

No production media path changes in this contract PR.
