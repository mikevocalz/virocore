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

#include <algorithm>
#include <capi/ViroContext_JNI.h>
#include "VROAndroidViewTexture.h"
#include "VROTextureSubstrateOpenGL.h"
#include "VROPlatformUtil.h"
#include "VRODriverOpenGL.h"

#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_AndroidViewTexture_##method_name

extern "C" {
VRO_METHOD(VRO_REF(VROAndroidViewTexture), nativeCreateAndroidViewTexture)(VRO_ARGS
                                                                          VRO_REF(ViroContext) context_j,
                                                                          VRO_INT width,
                                                                          VRO_INT height) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<ViroContext> context_w = VRO_REF_GET(ViroContext, context_j);
    std::shared_ptr<VROAndroidViewTexture> androidViewTexture
            = std::make_shared<VROAndroidViewTexture>(obj, width, height);

    VROPlatformDispatchAsyncRenderer([androidViewTexture, context_w] {
        std::shared_ptr<ViroContext> context = context_w.lock();
        if (!context) {
            return;
        }

        androidViewTexture->init(std::dynamic_pointer_cast<VRODriverOpenGL>(context->getDriver()));
    });

    return VRO_REF_NEW(VROAndroidViewTexture, androidViewTexture);
}

VRO_METHOD(void, nativeDeleteAndroidViewTexture)(VRO_ARGS
                                                VRO_REF(VROAndroidViewTexture) textureRef) {
    VRO_REF_DELETE(VROAndroidViewTexture, textureRef);
}

} // extern "C"

VROAndroidViewTexture::VROAndroidViewTexture(VRO_OBJECT jRenderableTexture, int width, int height):
        VROTexture(VROTextureType::TextureEGLImage, VROTextureInternalFormat::RGBA8, VROStereoMode::None),
        _textureId(0) {

    JNIEnv *env = VROPlatformGetJNIEnv();
    _jAndroidViewTexture = env->NewGlobalRef(jRenderableTexture);
    _jVideoSinkSurface = NULL;
    _height = height;
    _width = width;
}

VROAndroidViewTexture::~VROAndroidViewTexture() {
    JNIEnv *env = VROPlatformGetJNIEnv();
    env->DeleteGlobalRef(_jAndroidViewTexture);
    env->DeleteGlobalRef(_jVideoSinkSurface);
    VROPlatformDestroyVideoSink(_textureId);
    _jAndroidViewTexture = NULL;
    _jVideoSinkSurface = NULL;
}

void VROAndroidViewTexture::init(std::shared_ptr<VRODriverOpenGL> driver) {
    // Generate the GL Texture on which to render Android Views.
    glGenTextures(1, &_textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, _textureId);
    // GL_NEAREST minification made text panels shimmer and drop texels as soon
    // as the quad was viewed off-axis or at distance — a 2560×1600 source
    // minified to hundreds of pixels must filter, not pick. OES external
    // textures accept LINEAR (mipmaps are unsupported by the spec), and
    // anisotropic filtering sharpens the grazing angles XR panels live at.
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Aniso where the driver offers it (EXT_texture_filter_anisotropic is near-
    // universal on GLES3 XR runtimes); the query leaves maxAniso=0 on drivers
    // without the extension, so this is safe unconditionally.
    GLfloat maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 1.0f) {
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        std::min(maxAniso, 4.0f));
    }

    // Associated the substrate with the generated Texture
    std::unique_ptr<VROTextureSubstrate> substrate = std::unique_ptr<VROTextureSubstrateOpenGL>(
            new VROTextureSubstrateOpenGL(GL_TEXTURE_EXTERNAL_OES, _textureId, driver, true));
    setSubstrate(0, std::move(substrate));

    // Finally create the VideoSink's surface to be used for rendering our Android views onto.
    jobject jSurface = VROPlatformCreateVideoSink(_textureId, _width, _height);
    JNIEnv *env = VROPlatformGetJNIEnv();
    _jVideoSinkSurface = env->NewGlobalRef(jSurface);

    // Bind the VideoSink's surface to our RenderableAndroidTexture.java
    jclass cls = env->GetObjectClass(_jAndroidViewTexture);
    jmethodID jmethod = env->GetMethodID(cls, "setVideoSink", "(Landroid/view/Surface;)V");
    env->CallVoidMethod(_jAndroidViewTexture, jmethod, _jVideoSinkSurface);
    env->DeleteLocalRef(cls);
}