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

    std::weak_ptr<VRODriverOpenGL> _driver;

    bool createResolveResources(bool sRGBStorage);
    void resolve();
};

#endif /* VROAndroidViewTexture */
