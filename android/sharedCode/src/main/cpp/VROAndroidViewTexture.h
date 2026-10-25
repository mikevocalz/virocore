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
#include <memory>
class VRORenderContext;
class VROFrameSynchronizer;
class VRODriver;
class VRODriverOpenGL;

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
 */
class VROAndroidViewTexture : public VROTexture {
public:
    /*
     Constructs the VROAndroidViewTexture. After construction, init() must be
     asynchronously called on the rendering thread (from the UI thread).
     */
    VROAndroidViewTexture(VRO_OBJECT jRenderableTexture, int width, int height);
    ~VROAndroidViewTexture();

    /*
     Must be invoked from the rendering thread after construction.
     */
    void init(std::shared_ptr<VRODriverOpenGL> driver);

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

    bool createResolveResources(bool sRGBStorage);
    void resolve();
    // Copies the last latched frame again with the current corner and alpha
    // settings, on the rendering thread.
    void scheduleResolve();
};

#endif /* VROAndroidViewTexture */
