//
//  VROExternalSurfaceTexture.h
//  ViroRenderer
//
//  Bridges an externally-provided GPU buffer into a Viro material as a live
//  texture. Mirrors the camera-texture path: shader modifiers that sample
//  this texture set requiresExternalSurfaceTexture(true) so the engine
//  injects samplerExternalOES on Android.
//

#ifndef VROExternalSurfaceTexture_h
#define VROExternalSurfaceTexture_h

#include <cstdint>
#include <memory>
#include "VROTexture.h"
#include "NitroCanvasSharedTextureABI.h"

class VRODriver;
class VROFrameListener;
class VROFrameSynchronizer;
class VRONitroCanvasSource;
class VROTextureSubstrate;

/*
 Opaque handle to a platform-provided GPU buffer, and the wire struct the
 producer writes through the dlsym'd nitro_canvas_lookup_v2. This is an alias
 rather than our own declaration on purpose: NitroCanvasSharedTextureABI.h is
 vendored byte-identically from nitro-canvas-in-Vision/cpp/, so the producer
 cannot hand us a struct laid out differently from the one we allocate. The
 two sides used to hand-declare their own versions, which differed by a
 trailing int fenceFd — 28 bytes against 24 on armeabi-v7a.

 The fields are platform-disjoint: only the field for the current platform is
 meaningful.

   iosurface           iOS / visionOS: IOSurfaceRef as an integer. The
                       renderer wraps it in a CVPixelBuffer (via
                       kCVPixelBufferIOSurfacePropertiesKey) and imports it
                       through CVOpenGLESTextureCache (iOS) or
                       CVMetalTextureCache (visionOS).
   ahardwareBuffer     Android Route B: AHardwareBuffer* as an integer.
                       Imported via eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)
                       + glEGLImageTargetTexture2DOES into a
                       GL_TEXTURE_EXTERNAL_OES texture.
   surfaceTextureGLId  Android Route A: a SurfaceTexture whose owning Surface
                       the producer has already rendered into. The consumer
                       calls updateTexImage() on the existing
                       GL_TEXTURE_EXTERNAL_OES; no new substrate is produced.
   flags               NITRO_CANVAS_FLAG_SRGB replaces the old bool sRGB field.
   fenceFd             SSC v1.1 producer fence; -1 when there is none. The
                       importer owns the FD once the lookup returns it.

 Callers must zero the struct and set structSize + abiVersion before calling
 the lookup, or it refuses to write anything.
 */
using VROSharedTextureHandle = NitroCanvasSharedTextureHandle;

/*
 A VROTexture whose underlying GPU storage is supplied by an external producer.

   iOS:      IOSurface  → CVOpenGLESTextureCache → GL_TEXTURE_2D
   visionOS: IOSurface  → CVMetalTextureCache    → MTLTexture (VROTextureSubstrateMetal)
   Android:  AHardwareBuffer → EGLImageKHR        → GL_TEXTURE_EXTERNAL_OES (Route B)
             or SurfaceTexture                    → GL_TEXTURE_EXTERNAL_OES (Route A)

 The texture identity is stable for the lifetime of the CanvasSurface; the
 underlying substrate is swapped through updateFromHandle() when the
 producer's storage changes. On Android the texture type is TextureEGLImage,
 so a material using it as its diffuse texture gets the sampler2D →
 samplerExternalOES rewrite automatically. A custom shader modifier that
 samples it under another name still has to call
 setRequiresExternalSurfaceTexture(true) on itself.

 Two ways to feed it:

 1. Push: the caller imports each frame itself with updateFromHandle().
 2. Lookup by surface id (nitro-canvas-in-Vision rive.5 and later): construct
    with createForNitroCanvasSurface() and attach it to the renderer's frame
    synchronizer. Each frame, on the rendering thread and before anything is
    drawn, the texture calls the producer's nitro_canvas_lookup_v2 (resolved
    with dlsym, see VRONitroCanvasLookup), waits on the producer's fence, and
    imports new storage only when the buffer, size or colour flag changed.
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

    /*
     A texture bound to nitro-canvas-in-Vision surface `surfaceId` (the `id`
     of the JS CanvasSurface). width/height are the producer's current pixel
     size, used until the first frame arrives; the producer's handle is
     authoritative afterwards. Nothing is polled until
     attachToFrameSynchronizer() (or the caller drives
     updateFromNitroCanvasSurface() itself).
     */
    static std::shared_ptr<VROExternalSurfaceTexture>
        createForNitroCanvasSurface(int32_t surfaceId, int width, int height, bool sRGB = true);

    /*
     Poll the producer once and import what it published. Rendering thread,
     with the driver's context current. Returns true when a new frame was
     accepted. A no-op returning false for a texture not bound to a surface.
     */
    bool updateFromNitroCanvasSurface(std::shared_ptr<VRODriver> &driver);

    /*
     Register for onFrameWillRender so updateFromNitroCanvasSurface() runs
     every frame before rendering. The synchronizer keeps only a weak
     reference; the registration ends with this texture, or with
     detachFromFrameSynchronizer(). Rendering thread.
     */
    void attachToFrameSynchronizer(std::shared_ptr<VROFrameSynchronizer> frameSynchronizer,
                                   std::shared_ptr<VRODriver> driver);
    void detachFromFrameSynchronizer();

    // The bound surface id, or 0 for a push-mode texture.
    int32_t getNitroCanvasSurfaceId() const;

    // Producer frames accepted so far in lookup mode.
    uint64_t getNitroCanvasFrameCount() const;

    int getWidth() const { return _width; }
    int getHeight() const { return _height; }

private:
    int _width;
    int _height;
    bool _sRGB;

    std::unique_ptr<VRONitroCanvasSource> _nitroSource;
    std::shared_ptr<VROFrameListener> _frameListener;
    std::weak_ptr<VROFrameSynchronizer> _frameSynchronizer;
    bool _hasImportedStorage;
};

#endif /* VROExternalSurfaceTexture_h */
