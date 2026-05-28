//
//  VROExternalSurfaceTexture.h
//  ViroRenderer
//
//  Bridges an externally-provided GPU buffer (rendered into by Skia / WebGPU /
//  Rive on the JS side via the Nitro CanvasSurface HybridObject) into a Viro
//  material as a live texture. Mirrors the camera-texture path: shader
//  modifiers that sample this texture declare requiresExternalSurfaceTexture()
//  on the modifier, and the engine injects samplerExternalOES on Android.
//

#ifndef VROExternalSurfaceTexture_h
#define VROExternalSurfaceTexture_h

#include <cstdint>
#include <memory>
#include "VROTexture.h"

class VRODriver;
class VROTextureSubstrate;

/*
 Opaque handle to a platform-provided GPU buffer. The fields are
 platform-disjoint: only the field for the current platform is meaningful.
 */
struct VROSharedTextureHandle {
    int width = 0;
    int height = 0;
    bool sRGB = true;

    // iOS / visionOS: IOSurfaceRef cast to uintptr_t. The renderer wraps this
    // in a CVPixelBuffer (via kCVPixelBufferIOSurfacePropertiesKey) and then
    // imports it through CVOpenGLESTextureCache (iOS) or CVMetalTextureCache
    // (visionOS).
    uintptr_t iosurface = 0;

    // Android Route B: AHardwareBuffer* cast to uintptr_t. Imported via
    // eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID) + glEGLImageTargetTexture2DOES
    // into a GL_TEXTURE_EXTERNAL_OES texture.
    uintptr_t ahardwareBuffer = 0;

    // Android Route A: a SurfaceTexture whose owning Surface the producer has
    // already rendered into. The consumer just calls updateTexImage() on the
    // existing GL_TEXTURE_EXTERNAL_OES; no new substrate is produced.
    uint32_t surfaceTextureGLId = 0;
};

/*
 A VROTexture whose underlying GPU storage is supplied by an external producer.

   iOS:      IOSurface  → CVOpenGLESTextureCache → GL_TEXTURE_2D
   visionOS: IOSurface  → CVMetalTextureCache    → MTLTexture (VROTextureSubstrateMetal)
   Android:  AHardwareBuffer → EGLImageKHR        → GL_TEXTURE_EXTERNAL_OES (Route B)
             or SurfaceTexture                    → GL_TEXTURE_EXTERNAL_OES (Route A)

 The texture identity is stable for the lifetime of the CanvasSurface; the
 underlying substrate is hot-swapped on each producer frame via
 updateFromHandle(). On Android, shader modifiers that sample the texture
 must call setRequiresExternalSurfaceTexture(true) on themselves so the
 sampler2D → samplerExternalOES rewrite fires at shader compile time.
 */
class VROExternalSurfaceTexture : public VROTexture {
public:
    /*
     Construct an external surface texture sized to the producer's resolution.
     The first updateFromHandle() call attaches the actual substrate; before
     that, sampling yields the texture's default cleared content.
     */
    VROExternalSurfaceTexture(int width, int height, bool sRGB = true);
    ~VROExternalSurfaceTexture() override;

    /*
     Import the given platform handle into a fresh substrate and atomically
     swap it into substrate slot 0. Called from the rendering thread by the
     canvasSource bridge after the producer signals its frame is complete and
     the cross-API fence has been satisfied.
     */
    void updateFromHandle(const VROSharedTextureHandle &handle,
                          std::shared_ptr<VRODriver> &driver);

    int getWidth() const { return _width; }
    int getHeight() const { return _height; }

private:
    int _width;
    int _height;
    bool _sRGB;
};

#endif /* VROExternalSurfaceTexture_h */
