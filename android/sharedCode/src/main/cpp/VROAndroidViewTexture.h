//
//  VROAndroidViewTexture.h
//  ViroRenderer
//
//  Copyright © 2018 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef VROAndroidViewTexture_h
#define VROAndroidViewTexture_h

#include "VROTexture.h"
#include "VROOpenGL.h"
#include <atomic>
#include <cstdint>
#include <memory>
class VRORenderContext;
class VROFrameListener;
class VROFrameSynchronizer;
class VRODriver;
class VRODriverOpenGL;
class VROAHardwareBufferImage;
class VRONitroCanvasSource;
struct NitroCanvasSharedTextureHandle;

/*
 VROAndroidViewTexture is responsible for constructing and initializing
 surfaces and glTextures required for rendering Android pixel data onto
 VROTextures for a given AndroidViewTexture.java object in a 3D scene.

 The producer (an Android View, a Canvas/GPU renderer, MediaCodec) draws into
 a SurfaceTexture backed by a GL_TEXTURE_EXTERNAL_OES texture. External
 textures cannot carry mipmaps, so a large producer surface shown small in
 the headset was point- or bilinear-sampled from full resolution and text
 broke up. Each time the SurfaceTexture latches a new frame, the OES image is
 copied into a mipmapped GL_TEXTURE_2D (sRGB storage when the renderer works
 in linear space), and materials sample that texture with trilinear,
 anisotropic filtering.

 Alpha contract of the copy (viro-external ADR 0001 D6), chosen per texture:

 - Straight alpha (default; Android Views and every existing caller). The
   copy keeps colour and alpha as they are; sample it with VROBlendMode::Alpha.
 - Premultiplied alpha (setPremultipliedAlpha(true); Canvas, Skia and Rive
   producers). The copy stores premultiplied values and builds mips from
   them; sample it with VROBlendMode::PremultiplyAlpha. With sRGB storage the
   copy unpremultiplies, decodes to linear and premultiplies again, because
   the hardware decode of a premultiplied sRGB byte is not premultiplied
   linear: decode(a * s) != a * decode(s) for 0 < a < 1, and the difference is
   a dark fringe on every anti-aliased edge. With RGBA8 storage (non-linear
   rendering, blending in sRGB-encoded space) the bytes are already what the
   blend expects and are copied unchanged.

 Corners: setCornerRadius() makes the copy multiply by the shared coverage
 function (VROPanelCoverage.h), alpha only for straight input and all four
 channels for premultiplied input. Radius 0, the default, leaves every texel
 unchanged.

 Two sources feed the OES texture:

 - Surface (default): a VideoSink SurfaceTexture whose Surface is handed to
   the Java object through setVideoSink(Surface); each latched frame is
   resolved.
 - Nitro canvas lookup (setNitroCanvasSurface() before init()): no Surface is
   made. Every frame, before rendering, the texture calls the producer's
   nitro_canvas_lookup_v2 for its surface id (VRONitroCanvasSource). A new
   frame's AHardwareBuffer is bound to the OES texture through an EGLImage,
   re-imported only when the buffer, size or colour flag changes; the
   producer's fence is waited on (VROConsumeNativeFence) and the frame is
   resolved like a latched one. A producer that publishes a SurfaceTexture GL
   name instead (Route A) is resolved from that name directly. A size change
   rebuilds the resolve target at the producer's new size.
 */
/*
 Which Java class owns the texture. Only used to name the live-count log
 lines; both kinds behave the same.
 */
enum class VROAndroidViewTextureKind {
    AndroidView,     // com.viro.core.AndroidViewTexture
    ExternalSurface, // com.viro.core.ExternalSurfaceTexture
};

class VROAndroidViewTexture : public VROTexture {
public:
    /*
     Constructs the VROAndroidViewTexture. After construction, init() must be
     asynchronously called on the rendering thread (from the UI thread).
     */
    VROAndroidViewTexture(VRO_OBJECT jRenderableTexture, int width, int height,
                          VROAndroidViewTextureKind kind = VROAndroidViewTextureKind::AndroidView);
    ~VROAndroidViewTexture();

    /*
     Must be invoked from the rendering thread after construction.
     frameSynchronizer is required in nitro canvas lookup mode (it drives the
     per-frame lookup) and unused otherwise.
     */
    void init(std::shared_ptr<VRODriverOpenGL> driver,
              std::shared_ptr<VROFrameSynchronizer> frameSynchronizer = nullptr);

    /*
     Switch to nitro canvas lookup mode for `surfaceId` (a JS CanvasSurface
     id). Call once, after construction and before init(). Any thread.
     */
    void setNitroCanvasSurface(int32_t surfaceId);
    int32_t getNitroCanvasSurfaceId() const { return _nitroSurfaceId; }

    /*
     One lookup-mode frame: poll the producer, import, fence, resolve.
     Rendering thread. Returns true when a new frame was shown. Called by the
     frame listener init() registers; public for that listener only.
     */
    bool updateFromNitroCanvasSurface();

    /*
     Called on the rendering thread right after the SurfaceTexture whose OES
     texture is `oesTextureId` latched a new frame (VideoSink.updateTexImage).
     Refreshes the mipmapped copy. A no-op for textures that are not
     registered (for example the OES textures behind VROVideoTextureAVP).
     */
    static void onExternalFrameLatched(GLuint oesTextureId);

    /*
     Corner radius of the panel this texture is shown on, in meters, with the
     panel's size in meters. Converted to per-axis pixel radii with
     VROPanelCornerRadiiPx. 0 (the default) disables rounding. Any thread; the
     last latched frame is copied again on the rendering thread.
     */
    void setCornerRadius(float radiusMeters, float widthMeters, float heightMeters);

    /*
     Declares that the producer writes premultiplied alpha. Default false.
     Any thread; the last latched frame is copied again on the rendering thread.
     */
    void setPremultipliedAlpha(bool premultiplied);

private:
    /*
     The java object associated with this VROAndroidViewTexture that contains a reference
     to the Android views to be rendered.
     */
    jobject _jAndroidViewTexture;

    /*
     The VideoSink object used by the renderer to receive canvas pixel data to be rendered
     in the scene. This acts as the 'receiver' of pixel data provided by jAndroidViewTexture.
     */
    jobject _jVideoSinkSurface;

    /*
     The GL_TEXTURE_EXTERNAL_OES texture the SurfaceTexture writes into.
     */
    GLuint _textureId;

    /*
     The mipmapped GL_TEXTURE_2D materials sample, the framebuffer that
     writes its level 0, and the program and geometry that copy the OES
     image into it.
     */
    GLuint _resolveTexture;
    GLuint _resolveFramebuffer;
    GLuint _resolveProgram;
    GLuint _resolveVAO;
    GLuint _resolveVBO;
    bool _resolveReady;
    // A frame has been latched since init(); before that the OES texture has
    // no image and must not be sampled. Rendering thread only.
    bool _hasLatchedFrame;
    GLint _sizeUniform;
    GLint _radiusUniform;
    GLint _premultipliedUniform;

    // Written from any thread, read by resolve() on the rendering thread.
    std::atomic<float> _cornerRadiusXPx;
    std::atomic<float> _cornerRadiusYPx;
    std::atomic<bool> _premultipliedAlpha;

    std::weak_ptr<VRODriverOpenGL> _driver;

    const VROAndroidViewTextureKind _kind;

    // Lookup mode. _nitroSurfaceId is 0 in Surface mode.
    int32_t _nitroSurfaceId;
    std::unique_ptr<VRONitroCanvasSource> _nitroSource;
    std::unique_ptr<VROAHardwareBufferImage> _nitroImage;
    std::shared_ptr<VROFrameListener> _nitroFrameListener;
    // The OES name resolve() samples: _textureId, or the producer's own name
    // for a Route A (SurfaceTexture) frame.
    GLuint _resolveSourceTexture;
    bool _sRGBStorage;

    bool importNitroCanvasFrame(const NitroCanvasSharedTextureHandle &handle);
    // Rebuild the mipmapped target at a new producer size. Rendering thread.
    bool resizeResolveTarget(int width, int height);
    bool createResolveTarget(bool sRGBStorage);
    void destroyResolveTarget();
    bool createResolveResources(bool sRGBStorage);
    void resolve();
    // Copies the last latched frame again with the current corner and alpha
    // settings, on the rendering thread.
    void scheduleResolve();
};

#endif /* VROAndroidViewTexture */
