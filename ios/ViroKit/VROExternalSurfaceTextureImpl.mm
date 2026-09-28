// VROExternalSurfaceTextureImpl.mm
// ViroKit — iOS / visionOS
//
// Apple half of the shared-texture path: the producer's IOSurface becomes an
// MTLTexture, wrapped in a VROTextureSubstrateMetal. The Android half lives in
// android/sharedCode/src/main/cpp/VROExternalSurfaceTextureImpl.cpp and takes
// the AHardwareBuffer/SurfaceTexture routes instead.
//
// The import runs on the render thread, once per rendered frame, before the
// first eye encoder opens. Nothing here is thread-safe and nothing here needs
// to be: the render thread is the only owner, which is why there is no lock.

#include "VRODefines.h"

#if VRO_METAL

#include "VROExternalSurfaceTexture.h"
#include "VROTextureSubstrateMetal.h"
#include "VRODriverMetal.h"
#include "VROLog.h"

#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurfaceRef.h>
#include <Metal/Metal.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

/*
 How many frames a retired import has to outlive before its Metal and CoreVideo
 objects are released. CompositorServices keeps two drawables in flight and the
 renderer can be a frame ahead of the GPU, so three is one more frame than any
 encoded reference can survive. Nothing here waits on the GPU: the guard is a
 count of import() calls, and import() is called once per rendered frame.
 */
const uint64_t kRetiredFrameGuard = 3;

/*
 Upper bound on how many distinct IOSurfaces we keep imported. Producers cycle
 a double- or triple-buffered ring, so the steady state is two or three; the
 cap only exists so a producer that leaks surfaces cannot make this list grow
 without limit. Entries past the cap retire the same way resized ones do.
 */
const size_t kMaxCachedSurfaces = 4;

/*
 One producer IOSurface, imported once and reused for as long as the producer
 keeps handing the same surface back.

 The three references are one chain: the CVMetalTexture owns the MTLTexture,
 the CVPixelBuffer backs the CVMetalTexture, and the IOSurface backs the
 CVPixelBuffer. CVPixelBufferCreateWithIOSurface already retains the surface,
 but the surface arrives as a bare integer out of the ABI struct, so we take
 our own reference as well and drop it here — that reference, not the
 producer's goodwill, is what keeps the IOSurfaceRef from dangling.

 Held by std::shared_ptr and never copied: the substrate handed to the renderer
 holds one reference and the importer holds another, so the surface outlives
 whichever of the two is released first.
 */
class VROImportedSurface {

public:

    VROImportedSurface(IOSurfaceRef surface, IOSurfaceID surfaceId,
                       CVPixelBufferRef pixelBuffer, CVMetalTextureRef metalTexture) :
        _surface(surface),
        _surfaceId(surfaceId),
        _pixelBuffer(pixelBuffer),
        _metalTexture(metalTexture) {

        CFRetain(_surface);
    }

    ~VROImportedSurface() {
        CVBufferRelease(_metalTexture);
        CVPixelBufferRelease(_pixelBuffer);
        CFRelease(_surface);
    }

    VROImportedSurface(const VROImportedSurface &) = delete;
    VROImportedSurface &operator=(const VROImportedSurface &) = delete;

    IOSurfaceRef getSurface() const { return _surface; }
    IOSurfaceID  getSurfaceId() const { return _surfaceId; }

    id <MTLTexture> getTexture() const {
        return CVMetalTextureGetTexture(_metalTexture);
    }

private:

    IOSurfaceRef      _surface;
    IOSurfaceID       _surfaceId;
    CVPixelBufferRef  _pixelBuffer;
    CVMetalTextureRef _metalTexture;

};

/*
 The substrate installed into the VROExternalSurfaceTexture. It adds nothing to
 VROTextureSubstrateMetal but a reference to the import it samples from, which
 is the point: destroying it frees no GPU memory, so the render thread can swap
 substrates on any frame without stalling. The Metal objects are released by
 VROSharedTextureImporter, kRetiredFrameGuard frames after they stop being
 current.
 */
class VROExternalSurfaceSubstrateMetal : public VROTextureSubstrateMetal {

public:

    VROExternalSurfaceSubstrateMetal(std::shared_ptr<VROImportedSurface> surface) :
        VROTextureSubstrateMetal(surface->getTexture()),
        _surface(std::move(surface)) {
    }

private:

    std::shared_ptr<VROImportedSurface> _surface;

};

/*
 Render-thread-owned cache of imported IOSurfaces, keyed on IOSurface identity
 and invalidated by a resize generation.

 Per frame the importer does a linear scan of at most kMaxCachedSurfaces
 entries and, on a hit, constructs the substrate. The CVMetalTextureCache, the
 CVPixelBuffers and the MTLTextures are built on the first frame a surface is
 seen and then reused; none of them is rebuilt per frame. Note the deliberate
 absence of the CVMetalTextureCacheFlush-every-call pattern in
 VROVideoTextureCacheMetal — flushing on each import is what would throw the
 cache away again.
 */
class VROSharedTextureImporter {

public:

    ~VROSharedTextureImporter() {
        _live.clear();
        _retired.clear();
        if (_textureCache != nullptr) {
            CVMetalTextureCacheFlush(_textureCache, 0);
            CFRelease(_textureCache);
            _textureCache = nullptr;
        }
    }

    std::unique_ptr<VROTextureSubstrate> importHandle(const VROSharedTextureHandle &handle,
                                                      id <MTLDevice> device);

private:

    bool bindTextureCache(id <MTLDevice> device);
    std::shared_ptr<VROImportedSurface> acquire(IOSurfaceRef surface, MTLPixelFormat pixelFormat);
    void retire(const std::shared_ptr<VROImportedSurface> &entry);
    void drainRetired();

    CVMetalTextureCacheRef _textureCache = nullptr;

    /*
     Identity token for the device the texture cache was built against. Never
     messaged and never released: Metal vends one device per process and the
     driver holding it outlives the renderer, so this only answers "is the
     cache still the right one".
     */
    const void *_cacheDevice = nullptr;

    // The generation the live entries were imported under. A change in size or
    // in the sRGB flag retires all of them.
    int  _width = 0;
    int  _height = 0;
    bool _sRGB = false;

    // Import count, which is also the frame count: import() runs once a frame.
    uint64_t _frame = 0;

    // Live imports, most recently used first.
    std::vector<std::shared_ptr<VROImportedSurface>> _live;

    // Retired imports, each paired with the frame at which it may be released.
    std::vector<std::pair<uint64_t, std::shared_ptr<VROImportedSurface>>> _retired;

};

std::unique_ptr<VROTextureSubstrate>
VROSharedTextureImporter::importHandle(const VROSharedTextureHandle &handle,
                                       id <MTLDevice> device) {
    ++_frame;
    drainRetired();

    IOSurfaceRef surface = reinterpret_cast<IOSurfaceRef>(static_cast<uintptr_t>(handle.iosurface));
    if (surface == nullptr) {
        pinfo("VROExternalSurfaceTexture: Apple handle has no IOSurface set");
        return nullptr;
    }
    if (handle.width <= 0 || handle.height <= 0) {
        pinfo("VROExternalSurfaceTexture: Apple handle has degenerate size %dx%d",
              handle.width, handle.height);
        return nullptr;
    }

    /*
     The producer's fence is not honoured. handle.fenceFd carries an
     MTLSharedEvent *signaled value* and the ABI carries no way to reach the
     event itself: NitroCanvasSharedTextureHandle has no MTLSharedEventHandle,
     no mach port and no shared-event name, and an MTLSharedEvent cannot be
     rebuilt from a value alone. With the event in hand the wait belongs on the
     frame's command buffer —
     [commandBuffer encodeWaitForEvent:event value:handle.fenceFd] via
     VRODriverVisionOS::getFrameCommandBuffer(), issued before the first eye
     encoder opens — and not on the CPU, which would stall the render thread.
     Until the ABI carries the event, sampling can race a producer that is
     still writing the surface. Faking the wait would hide that; skipping it
     does not.
     */

    // BGRA8 is the only layout the producer publishes and the only one this
    // import handles, so a surface in any other format is rejected rather than
    // reinterpreted.
    const OSType surfaceFormat = IOSurfaceGetPixelFormat(surface);
    if (surfaceFormat != kCVPixelFormatType_32BGRA) {
        pinfo("VROExternalSurfaceTexture: IOSurface pixel format 0x%x is not 32BGRA",
              (unsigned int) surfaceFormat);
        return nullptr;
    }

    if (!bindTextureCache(device)) {
        return nullptr;
    }

    /*
     MTLPixelFormatBGRA8Unorm_sRGB when the producer set NITRO_CANVAS_FLAG_SRGB,
     MTLPixelFormatBGRA8Unorm otherwise. The bytes are the same either way; the
     _sRGB view is what makes the sampler decode them to linear, which is what
     the rest of the frame is in — VRODriverVisionOS reports
     VROColorRenderingMode::Linear and CompositorServices composites linearly,
     so sampling sRGB bytes through the plain Unorm view would show up as a
     washed-out panel.

     Alpha is premultiplied, and there is no Metal pixel format that says so:
     premultiplication is a blend-state convention, not a format. This import
     passes the producer's pixels through untouched, so a material sampling
     this texture has to blend as premultiplied — un-premultiplying here would
     mean a pass over every pixel of every frame.
     */
    const bool sRGB = (handle.flags & NITRO_CANVAS_FLAG_SRGB) != 0;
    const MTLPixelFormat pixelFormat = sRGB ? MTLPixelFormatBGRA8Unorm_sRGB
                                            : MTLPixelFormatBGRA8Unorm;

    if (handle.width != _width || handle.height != _height || sRGB != _sRGB) {
        _width  = handle.width;
        _height = handle.height;
        _sRGB   = sRGB;

        // Every live import describes the old size or the old colour space.
        // They go through the retire list rather than being dropped here,
        // because the frame that last sampled them may still be in flight.
        for (const std::shared_ptr<VROImportedSurface> &entry : _live) {
            retire(entry);
        }
        _live.clear();
    }

    std::shared_ptr<VROImportedSurface> entry = acquire(surface, pixelFormat);
    if (!entry) {
        return nullptr;
    }

    // The substrate is the one allocation per frame this path cannot avoid:
    // VROTexture::setSubstrate takes ownership of a unique_ptr. It is a handful
    // of bytes holding a shared_ptr, and it touches no GPU memory.
    return std::unique_ptr<VROTextureSubstrate>(new VROExternalSurfaceSubstrateMetal(std::move(entry)));
}

bool VROSharedTextureImporter::bindTextureCache(id <MTLDevice> device) {
    if (device == nil) {
        pinfo("VROExternalSurfaceTexture: Metal driver has no device");
        return false;
    }

    const void *deviceIdentity = (__bridge const void *) device;
    if (_textureCache != nullptr && _cacheDevice == deviceIdentity) {
        return true;
    }

    if (_textureCache != nullptr) {
        // A different device means every import made against the old one is
        // unusable. Retire rather than release, for the same in-flight reason.
        for (const std::shared_ptr<VROImportedSurface> &entry : _live) {
            retire(entry);
        }
        _live.clear();
        CVMetalTextureCacheFlush(_textureCache, 0);
        CFRelease(_textureCache);
        _textureCache = nullptr;
    }

    CVReturn result = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device,
                                                nullptr, &_textureCache);
    if (result != kCVReturnSuccess || _textureCache == nullptr) {
        _textureCache = nullptr;
        pinfo("VROExternalSurfaceTexture: CVMetalTextureCacheCreate failed (%d)", (int) result);
        return false;
    }

    _cacheDevice = deviceIdentity;
    return true;
}

std::shared_ptr<VROImportedSurface>
VROSharedTextureImporter::acquire(IOSurfaceRef surface, MTLPixelFormat pixelFormat) {
    /*
     IOSurfaceGetID is the identity key. IDs are recycled once a surface dies,
     which would make a stale ID match a new surface — except that every live
     entry holds a reference to its surface, so no ID in this list can have
     been recycled. The pointer is compared as well, so a recycled ID from a
     surface we did not retain still misses.
     */
    const IOSurfaceID surfaceId = IOSurfaceGetID(surface);
    for (size_t i = 0; i < _live.size(); i++) {
        if (_live[i]->getSurfaceId() == surfaceId && _live[i]->getSurface() == surface) {
            std::shared_ptr<VROImportedSurface> hit = _live[i];
            _live.erase(_live.begin() + i);
            _live.insert(_live.begin(), hit);
            return hit;
        }
    }

    CVPixelBufferRef pixelBuffer = nullptr;
    CVReturn result = CVPixelBufferCreateWithIOSurface(kCFAllocatorDefault, surface,
                                                       nullptr, &pixelBuffer);
    if (result != kCVReturnSuccess || pixelBuffer == nullptr) {
        pinfo("VROExternalSurfaceTexture: CVPixelBufferCreateWithIOSurface failed (%d)", (int) result);
        return nullptr;
    }

    // Size comes from the surface, not from the handle: the handle's size is
    // what the producer intends to draw, the surface's is what it allocated,
    // and the texture has to describe the allocation.
    CVMetalTextureRef metalTexture = nullptr;
    result = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, _textureCache,
                                                       pixelBuffer, nullptr, pixelFormat,
                                                       CVPixelBufferGetWidth(pixelBuffer),
                                                       CVPixelBufferGetHeight(pixelBuffer),
                                                       0, &metalTexture);
    if (result != kCVReturnSuccess || metalTexture == nullptr) {
        CVPixelBufferRelease(pixelBuffer);
        pinfo("VROExternalSurfaceTexture: CVMetalTextureCacheCreateTextureFromImage failed (%d)",
              (int) result);
        return nullptr;
    }
    if (CVMetalTextureGetTexture(metalTexture) == nil) {
        CVBufferRelease(metalTexture);
        CVPixelBufferRelease(pixelBuffer);
        pinfo("VROExternalSurfaceTexture: CVMetalTexture carries no MTLTexture");
        return nullptr;
    }

    std::shared_ptr<VROImportedSurface> entry =
        std::make_shared<VROImportedSurface>(surface, surfaceId, pixelBuffer, metalTexture);
    _live.insert(_live.begin(), entry);

    while (_live.size() > kMaxCachedSurfaces) {
        retire(_live.back());
        _live.pop_back();
    }
    return entry;
}

void VROSharedTextureImporter::retire(const std::shared_ptr<VROImportedSurface> &entry) {
    _retired.push_back(std::make_pair(_frame + kRetiredFrameGuard, entry));
}

void VROSharedTextureImporter::drainRetired() {
    if (_retired.empty()) {
        return;
    }

    const size_t before = _retired.size();
    const uint64_t frame = _frame;
    _retired.erase(std::remove_if(_retired.begin(), _retired.end(),
                                  [frame](const std::pair<uint64_t, std::shared_ptr<VROImportedSurface>> &retired) {
                                      return retired.first <= frame;
                                  }),
                   _retired.end());

    // Only worth doing when something actually went away: this is what lets
    // CoreVideo drop its own bookkeeping for the released textures.
    if (_retired.size() != before && _textureCache != nullptr) {
        CVMetalTextureCacheFlush(_textureCache, 0);
    }
}

} // namespace

std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver) {
    std::shared_ptr<VRODriverMetal> metalDriver = std::dynamic_pointer_cast<VRODriverMetal>(driver);
    if (!metalDriver) {
        pinfo("VROExternalSurfaceTexture: driver is not Metal, cannot import an IOSurface");
        return nullptr;
    }

    // One importer for the process, reached only from the render thread.
    static VROSharedTextureImporter importer;
    return importer.importHandle(handle, metalDriver->getDevice());
}

#else

#include "VROExternalSurfaceTexture.h"
#include "VROTextureSubstrate.h"
#include "VROLog.h"

/*
 iOS builds ViroKit against OpenGL ES, where the import would go
 IOSurface -> CVPixelBuffer -> CVOpenGLESTextureCache -> GL_TEXTURE_2D, as
 VROExternalSurfaceTexture.h describes. That route is not written. The symbol
 is defined anyway so the OpenGL ES build of ViroKit links, and returning null
 is the "skip this frame" answer the caller already handles.
 */
std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver) {
    pinfo("VROExternalSurfaceTexture: the OpenGL ES import route is not implemented on iOS");
    return nullptr;
}

#endif // VRO_METAL
