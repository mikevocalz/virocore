//
//  VRONitroCanvasSource.h
//  ViroRenderer
//
//  Per-surface polling state for a texture bound to a nitro-canvas-in-Vision
//  surface id. Platform-neutral: it decides when there is a new frame and
//  whether the producer's storage changed, and leaves the GPU import to the
//  platform texture that owns it.
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license.
//

#ifndef VRONitroCanvasSource_h
#define VRONitroCanvasSource_h

#include <cstdint>
#include <memory>
#include "NitroCanvasSharedTextureABI.h"

struct VRONitroCanvasChannel;

/*
 One new producer frame, as returned by VRONitroCanvasSource::poll().

   handle          Validated handle. The receiver owns handle.fenceFd and must
                   wait on it (and on Android close it) before sampling.
   storageChanged  The backing object or its description differs from the
                   last frame: another AHardwareBuffer / IOSurface / GL name,
                   another size, or another sRGB flag. The receiver has to
                   import the new storage. When false, the storage it imported
                   earlier already aliases the new pixels; only the fence and
                   any resolve copy are needed.
 */
struct VRONitroCanvasFrame {
    NitroCanvasSharedTextureHandle handle;
    bool storageChanged;
};

/*
 Signature of the lookup the source calls. Defaults to
 VRONitroCanvasLookup::lookup; tests pass their own. Returns true only for a
 validated new frame, with the fence owned by the caller.
 */
typedef bool (*VRONitroCanvasPollFn)(int32_t surfaceId, NitroCanvasSharedTextureHandle *out);

/*
 Several textures may bind the same surface id (the same canvas on two
 materials, or a texture re-created before the old one is collected). The
 producer hands each published frame out once, so sources on one id share a
 process-wide channel: whichever polls first takes the frame from the
 producer, and the others receive the same handle on their next poll. Only
 the source that took it from the producer gets the fence; the rest share the
 rendering thread's GL context, where that wait is already queued ahead of
 them.
 */
class VRONitroCanvasSource {
public:
    explicit VRONitroCanvasSource(int32_t surfaceId, VRONitroCanvasPollFn pollFn = nullptr);
    ~VRONitroCanvasSource();

    int32_t getSurfaceId() const { return _surfaceId; }

    /*
     Ask the producer for a frame published since the last call. Returns true
     and fills `out` when there is one. Call once per rendered frame, on the
     rendering thread, before anything samples the texture. Not thread-safe;
     one source belongs to one texture and one thread.
     */
    bool poll(VRONitroCanvasFrame *out);

    /*
     Forget the storage seen so far, so the next frame reports
     storageChanged. Call when the importer lost what it imported (GL context
     loss, a failed import).
     */
    void invalidateStorage() { _hasStorage = false; }

    // Frames accepted by poll() so far.
    uint64_t getFrameCount() const { return _frameCount; }

private:
    const int32_t _surfaceId;
    VRONitroCanvasPollFn _pollFn;
    std::shared_ptr<VRONitroCanvasChannel> _channel;
    uint64_t _seenGeneration;

    bool _hasStorage;
    uint64_t _iosurface;
    uint64_t _ahardwareBuffer;
    uint32_t _surfaceTextureGLId;
    int32_t _width;
    int32_t _height;
    uint32_t _flags;
    uint64_t _frameCount;
};

#endif /* VRONitroCanvasSource_h */
