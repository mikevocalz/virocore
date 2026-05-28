//
//  VROExternalSurfaceTextureImpl.mm
//  ViroKit
//
//  Apple-platform implementation of VROExternalSurfaceTextureImpl_importHandle().
//  iOS uses CVOpenGLESTextureCache; visionOS uses CVMetalTextureCache. Caches
//  are kept per-device. Each substrate owns its own CV texture ref and
//  releases it on destruction.
//

#include "VROExternalSurfaceTexture.h"
#include "VROTextureSubstrateOpenGL.h"
#include "VRODriver.h"
#include "VROLog.h"

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>

#include <map>
#include <mutex>

#if TARGET_OS_VISION
  #import <Metal/Metal.h>
  #include "VROTextureSubstrateMetal.h"
  #include "VRODriverMetal.h"
#else
  #import <OpenGLES/EAGL.h>
  #include "VRODriverOpenGLiOS.h"
#endif

#if TARGET_OS_VISION

// visionOS: IOSurface -> MTLTexture via CVMetalTextureCache.

namespace {

CVMetalTextureCacheRef cacheFor(id<MTLDevice> device) {
    static std::mutex mu;
    static std::map<void *, CVMetalTextureCacheRef> caches;
    std::lock_guard<std::mutex> lock(mu);
    void *key = (__bridge void *)device;
    auto it = caches.find(key);
    if (it != caches.end()) return it->second;
    CVMetalTextureCacheRef cache = nullptr;
    CVReturn err = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &cache);
    if (err != kCVReturnSuccess || cache == nullptr) {
        pinfo("VROExternalSurfaceTexture: CVMetalTextureCacheCreate failed: %d", (int)err);
        return nullptr;
    }
    caches[key] = cache;
    return cache;
}

// Substrate that owns a CVMetalTextureRef and releases it on destruction.
class VROExternalSurfaceSubstrateMetal : public VROTextureSubstrateMetal {
public:
    VROExternalSurfaceSubstrateMetal(id<MTLTexture> texture, CVMetalTextureRef ref)
        : VROTextureSubstrateMetal(texture), _ref(ref) {}
    ~VROExternalSurfaceSubstrateMetal() override {
        if (_ref) CFRelease(_ref);
    }
private:
    CVMetalTextureRef _ref;
};

} // namespace

std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver) {
    if (handle.iosurface == 0) {
        pinfo("VROExternalSurfaceTexture: visionOS handle missing iosurface");
        return nullptr;
    }
    auto *metalDriver = dynamic_cast<VRODriverMetal *>(driver.get());
    if (!metalDriver) {
        pinfo("VROExternalSurfaceTexture: visionOS requires a VRODriverMetal");
        return nullptr;
    }
    id<MTLDevice> device = metalDriver->getDevice();
    CVMetalTextureCacheRef cache = cacheFor(device);
    if (!cache) return nullptr;

    IOSurfaceRef iosurf = (IOSurfaceRef)(uintptr_t)handle.iosurface;
    NSDictionary *attrs = @{ (id)kCVPixelBufferIOSurfacePropertiesKey: @{} };
    CVPixelBufferRef pb = nullptr;
    CVReturn err = CVPixelBufferCreateWithIOSurface(kCFAllocatorDefault, iosurf,
                                                    (__bridge CFDictionaryRef)attrs, &pb);
    if (err != kCVReturnSuccess || pb == nullptr) {
        pinfo("VROExternalSurfaceTexture: CVPixelBufferCreateWithIOSurface failed: %d", (int)err);
        return nullptr;
    }

    CVMetalTextureRef ref = nullptr;
    err = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, cache, pb, nullptr,
        handle.sRGB ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm,
        handle.width, handle.height, 0, &ref);
    CVPixelBufferRelease(pb);

    if (err != kCVReturnSuccess || ref == nullptr) {
        pinfo("VROExternalSurfaceTexture: CVMetalTextureCacheCreateTextureFromImage failed: %d", (int)err);
        return nullptr;
    }
    id<MTLTexture> tex = CVMetalTextureGetTexture(ref);
    if (!tex) {
        CFRelease(ref);
        pinfo("VROExternalSurfaceTexture: CVMetalTextureGetTexture returned nil");
        return nullptr;
    }
    return std::make_unique<VROExternalSurfaceSubstrateMetal>(tex, ref);
}

#else  // !TARGET_OS_VISION

// iOS: IOSurface -> GL_TEXTURE_2D via CVOpenGLESTextureCache.

namespace {

CVOpenGLESTextureCacheRef cacheFor(EAGLContext *ctx) {
    static std::mutex mu;
    static std::map<void *, CVOpenGLESTextureCacheRef> caches;
    std::lock_guard<std::mutex> lock(mu);
    void *key = (__bridge void *)ctx;
    auto it = caches.find(key);
    if (it != caches.end()) return it->second;
    CVOpenGLESTextureCacheRef cache = nullptr;
    CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, nullptr, ctx, nullptr, &cache);
    if (err != kCVReturnSuccess || cache == nullptr) {
        pinfo("VROExternalSurfaceTexture: CVOpenGLESTextureCacheCreate failed: %d", (int)err);
        return nullptr;
    }
    caches[key] = cache;
    return cache;
}

// Substrate that owns a CVOpenGLESTextureRef and releases it on destruction.
// ownTexture=false: the GL texture name is owned by the CV ref, not by the
// substrate; releasing the CV ref is what tears down the GL texture.
class VROExternalSurfaceSubstrateGL : public VROTextureSubstrateOpenGL {
public:
    VROExternalSurfaceSubstrateGL(GLenum target, GLuint name,
                                  std::shared_ptr<VRODriver> driver,
                                  CVOpenGLESTextureRef ref)
        : VROTextureSubstrateOpenGL(target, name, driver, /*ownTexture*/ false),
          _ref(ref) {}
    ~VROExternalSurfaceSubstrateGL() override {
        if (_ref) CFRelease(_ref);
    }
private:
    CVOpenGLESTextureRef _ref;
};

} // namespace

std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver) {
    if (handle.iosurface == 0) {
        pinfo("VROExternalSurfaceTexture: iOS handle missing iosurface");
        return nullptr;
    }
    auto *iosDriver = dynamic_cast<VRODriverOpenGLiOS *>(driver.get());
    if (!iosDriver) {
        pinfo("VROExternalSurfaceTexture: iOS requires a VRODriverOpenGLiOS");
        return nullptr;
    }
    EAGLContext *ctx = iosDriver->getEAGLContext();
    CVOpenGLESTextureCacheRef cache = cacheFor(ctx);
    if (!cache) return nullptr;

    IOSurfaceRef iosurf = (IOSurfaceRef)(uintptr_t)handle.iosurface;
    NSDictionary *attrs = @{ (id)kCVPixelBufferIOSurfacePropertiesKey: @{} };
    CVPixelBufferRef pb = nullptr;
    CVReturn err = CVPixelBufferCreateWithIOSurface(kCFAllocatorDefault, iosurf,
                                                    (__bridge CFDictionaryRef)attrs, &pb);
    if (err != kCVReturnSuccess || pb == nullptr) {
        pinfo("VROExternalSurfaceTexture: CVPixelBufferCreateWithIOSurface failed: %d", (int)err);
        return nullptr;
    }

    // Pre-emptive flush of any retired refs in the cache.
    CVOpenGLESTextureCacheFlush(cache, 0);

    CVOpenGLESTextureRef ref = nullptr;
    err = CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, cache, pb, nullptr,
        GL_TEXTURE_2D,
        handle.sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA,
        handle.width, handle.height,
        GL_BGRA, GL_UNSIGNED_BYTE, 0, &ref);
    CVPixelBufferRelease(pb);

    if (err != kCVReturnSuccess || ref == nullptr) {
        pinfo("VROExternalSurfaceTexture: CVOpenGLESTextureCacheCreateTextureFromImage failed: %d", (int)err);
        return nullptr;
    }
    GLuint name = CVOpenGLESTextureGetName(ref);
    GLenum target = CVOpenGLESTextureGetTarget(ref);
    if (name == 0) {
        CFRelease(ref);
        pinfo("VROExternalSurfaceTexture: CVOpenGLESTextureGetName returned 0");
        return nullptr;
    }
    return std::make_unique<VROExternalSurfaceSubstrateGL>(target, name, driver, ref);
}

#endif // TARGET_OS_VISION
