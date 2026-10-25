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
#include "VROLog.h"
#include "VROPanelCoverage.h"
#include "VROPanelGLStateGuard.h"
#include "VROPanelShaderSources.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_AndroidViewTexture_##method_name

namespace {

// Anisotropy for the resolved texture. Panels are seen at oblique angles as
// the head moves; 4x keeps glyph strokes crisp there at a small cost.
constexpr GLfloat kResolveAnisotropy = 4.0f;

// OES texture name -> the texture that resolves it. Registered on the
// rendering thread in init(); looked up on the rendering thread after each
// updateTexImage(); pruned from any thread when a texture is destroyed.
std::mutex sRegistryMutex;
std::unordered_map<GLuint, std::weak_ptr<VROTexture>> sRegistry;

// Live instances per kind, logged when one is created or destroyed (never per
// frame). Each instance owns one SurfaceTexture/Surface pair (the VideoSink
// made in init() and destroyed in the destructor), so after a reload or a
// background/foreground cycle the count should return to its earlier value.
std::atomic<int> sLiveAndroidView{0};
std::atomic<int> sLiveExternalSurface{0};

std::atomic<int> &liveCount(VROAndroidViewTextureKind kind) {
    return kind == VROAndroidViewTextureKind::ExternalSurface ? sLiveExternalSurface
                                                              : sLiveAndroidView;
}

const char *kindName(VROAndroidViewTextureKind kind) {
    return kind == VROAndroidViewTextureKind::ExternalSurface ? "ExternalSurfaceTexture"
                                                              : "AndroidViewTexture";
}

GLuint compileShader(GLenum type, const std::string &source) {
    GLuint shader = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        pinfo("VROAndroidViewTexture: resolve shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool hasExtension(const char *name) {
    const char *extensions = (const char *) glGetString(GL_EXTENSIONS);
    return extensions != nullptr && strstr(extensions, name) != nullptr;
}

} // namespace

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

// Called by VideoSink.onDrawFrame() on the rendering thread, right after
// SurfaceTexture.updateTexImage() latched a new frame.
JNIEXPORT void JNICALL
Java_com_viro_core_internal_VideoSink_nativeOnFrameLatched(JNIEnv *env, jclass clazz,
                                                          jint textureId) {
    VROAndroidViewTexture::onExternalFrameLatched((GLuint) textureId);
}

} // extern "C"

VROAndroidViewTexture::VROAndroidViewTexture(VRO_OBJECT jRenderableTexture, int width, int height,
                                             VROAndroidViewTextureKind kind):
        // Materials sample the mipmapped GL_TEXTURE_2D copy, not the OES image,
        // so this is an ordinary 2D texture to the shader pipeline.
        VROTexture(VROTextureType::Texture2D, VROTextureInternalFormat::RGBA8, VROStereoMode::None),
        _textureId(0),
        _resolveTexture(0),
        _resolveFramebuffer(0),
        _resolveProgram(0),
        _resolveVAO(0),
        _resolveVBO(0),
        _resolveReady(false),
        _hasLatchedFrame(false),
        _sizeUniform(-1),
        _radiusUniform(-1),
        _premultipliedUniform(-1),
        _cornerRadiusXPx(0.0f),
        _cornerRadiusYPx(0.0f),
        _premultipliedAlpha(false),
        _kind(kind) {

    JNIEnv *env = VROPlatformGetJNIEnv();
    _jAndroidViewTexture = env->NewGlobalRef(jRenderableTexture);
    _jVideoSinkSurface = NULL;
    _height = height;
    _width = width;
    const int live = ++liveCount(_kind);
    pinfo("%s: live=%d (created %dx%d)", kindName(_kind), live, width, height);
}

VROAndroidViewTexture::~VROAndroidViewTexture() {
    JNIEnv *env = VROPlatformGetJNIEnv();
    env->DeleteGlobalRef(_jAndroidViewTexture);
    env->DeleteGlobalRef(_jVideoSinkSurface);
    VROPlatformDestroyVideoSink(_textureId);
    _jAndroidViewTexture = NULL;
    _jVideoSinkSurface = NULL;
    const int live = --liveCount(_kind);
    pinfo("%s: live=%d (destroyed %dx%d)", kindName(_kind), live, _width, _height);

    {
        // Our own entry is already expired (no strong references remain). A
        // live entry under the same name belongs to a newer texture.
        std::lock_guard<std::mutex> lock(sRegistryMutex);
        auto it = sRegistry.find(_textureId);
        if (it != sRegistry.end() && it->second.expired()) {
            sRegistry.erase(it);
        }
    }

    // The substrate owns _resolveTexture. Everything else is ours; queue it
    // for deletion on the rendering thread.
    std::shared_ptr<VRODriverOpenGL> driver = _driver.lock();
    if (driver) {
        if (_textureId) driver->deleteTexture(_textureId);
        // Without a substrate (resolve setup failed) nothing else owns it.
        if (_resolveTexture && !_resolveReady) driver->deleteTexture(_resolveTexture);
        if (_resolveFramebuffer) driver->deleteFramebuffer(_resolveFramebuffer);
        if (_resolveProgram) driver->deleteProgram(_resolveProgram);
        if (_resolveVAO) driver->deleteVertexArray(_resolveVAO);
        if (_resolveVBO) driver->deleteBuffer(_resolveVBO);
    }
}

void VROAndroidViewTexture::init(std::shared_ptr<VRODriverOpenGL> driver) {
    _driver = driver;
    VROPanelGLStateGuard guard;

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

    // sRGB storage when the renderer works in linear space (sRGB swapchain):
    // sampling decodes in hardware, matching every other image texture. On an
    // RGBA8 swapchain (NonLinear mode) store the bytes as-is, as before.
    bool sRGBStorage = driver && driver->isLinearRenderingEnabled();
    if (createResolveResources(sRGBStorage)) {
        std::unique_ptr<VROTextureSubstrate> substrate = std::unique_ptr<VROTextureSubstrateOpenGL>(
                new VROTextureSubstrateOpenGL(GL_TEXTURE_2D, _resolveTexture, driver, true));
        setSubstrate(0, std::move(substrate));
        _resolveReady = true;

        std::lock_guard<std::mutex> lock(sRegistryMutex);
        sRegistry[_textureId] = shared_from_this();
    } else {
        perr("VROAndroidViewTexture: could not build the %dx%d mipmapped resolve target",
             _width, _height);
    }

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

bool VROAndroidViewTexture::createResolveResources(bool sRGBStorage) {
    if (_width <= 0 || _height <= 0) {
        return false;
    }

    // Full mip chain down to 1x1.
    int largest = std::max(_width, _height);
    GLsizei levels = (GLsizei) std::floor(std::log2((double) largest)) + 1;

    glGenTextures(1, &_resolveTexture);
    glBindTexture(GL_TEXTURE_2D, _resolveTexture);
    glTexStorage2D(GL_TEXTURE_2D, levels, sRGBStorage ? GL_SRGB8_ALPHA8 : GL_RGBA8, _width, _height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (hasExtension("GL_EXT_texture_filter_anisotropic")) {
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        std::min(kResolveAnisotropy, maxAnisotropy));
    }

    glGenFramebuffers(1, &_resolveFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _resolveFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _resolveTexture, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        perr("VROAndroidViewTexture: resolve framebuffer incomplete (0x%x)", status);
        return false;
    }

    // Start transparent rather than with undefined contents.
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, _width, _height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glGenerateMipmap(GL_TEXTURE_2D);

    std::string fragmentSource = VROPanelResolveFragmentShader(sRGBStorage);
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVROPanelResolveVertexShader);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertexShader || !fragmentShader) {
        if (vertexShader) glDeleteShader(vertexShader);
        if (fragmentShader) glDeleteShader(fragmentShader);
        return false;
    }

    _resolveProgram = glCreateProgram();
    glAttachShader(_resolveProgram, vertexShader);
    glAttachShader(_resolveProgram, fragmentShader);
    glBindAttribLocation(_resolveProgram, 0, "a_position");
    glLinkProgram(_resolveProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(_resolveProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512] = { 0 };
        glGetProgramInfoLog(_resolveProgram, sizeof(log), nullptr, log);
        perr("VROAndroidViewTexture: resolve program link failed: %s", log);
        return false;
    }
    glUseProgram(_resolveProgram);
    glUniform1i(glGetUniformLocation(_resolveProgram, "source_texture"), 0);
    _sizeUniform = glGetUniformLocation(_resolveProgram, "u_size");
    _radiusUniform = glGetUniformLocation(_resolveProgram, "u_radius");
    _premultipliedUniform = glGetUniformLocation(_resolveProgram, "u_premultiplied");

    // One triangle that covers the whole target.
    const GLfloat triangle[] = { -1.0f, -1.0f,  3.0f, -1.0f,  -1.0f, 3.0f };
    glGenVertexArrays(1, &_resolveVAO);
    glBindVertexArray(_resolveVAO);
    glGenBuffers(1, &_resolveVBO);
    glBindBuffer(GL_ARRAY_BUFFER, _resolveVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    pinfo("VROAndroidViewTexture: %dx%d resolve target, %d mip levels, %s storage",
          _width, _height, (int) levels, sRGBStorage ? "sRGB" : "RGBA8");
    return true;
}

void VROAndroidViewTexture::onExternalFrameLatched(GLuint oesTextureId) {
    std::weak_ptr<VROTexture> weak;
    {
        std::lock_guard<std::mutex> lock(sRegistryMutex);
        auto it = sRegistry.find(oesTextureId);
        if (it == sRegistry.end()) {
            return;
        }
        weak = it->second;
    }
    std::shared_ptr<VROTexture> texture = weak.lock();
    if (texture) {
        std::shared_ptr<VROAndroidViewTexture> viewTexture =
                std::static_pointer_cast<VROAndroidViewTexture>(texture);
        viewTexture->_hasLatchedFrame = true;
        viewTexture->resolve();
    }
}

void VROAndroidViewTexture::setCornerRadius(float radiusMeters, float widthMeters,
                                            float heightMeters) {
    VROPanelCornerRadii radii = VROPanelCornerRadiiPx(radiusMeters, widthMeters, heightMeters,
                                                      (float) _width, (float) _height);
    _cornerRadiusXPx.store(radii.x);
    _cornerRadiusYPx.store(radii.y);
    scheduleResolve();
}

void VROAndroidViewTexture::setPremultipliedAlpha(bool premultiplied) {
    _premultipliedAlpha.store(premultiplied);
    scheduleResolve();
}

void VROAndroidViewTexture::scheduleResolve() {
    // A static producer queues no new frame, so the last latched one is
    // copied again. The OES texture keeps that image until the next latch.
    std::weak_ptr<VROTexture> weak = shared_from_this();
    VROPlatformDispatchAsyncRenderer([weak] {
        std::shared_ptr<VROTexture> texture = weak.lock();
        if (!texture) {
            return;
        }
        std::shared_ptr<VROAndroidViewTexture> viewTexture =
                std::static_pointer_cast<VROAndroidViewTexture>(texture);
        if (viewTexture->_hasLatchedFrame) {
            viewTexture->resolve();
        }
    });
}

void VROAndroidViewTexture::resolve() {
    if (!_resolveReady) {
        return;
    }
    VROPanelGLStateGuard guard;

    glBindFramebuffer(GL_FRAMEBUFFER, _resolveFramebuffer);
    // The copy overwrites every texel, so skip loading the old tile contents.
    const GLenum colorAttachment = GL_COLOR_ATTACHMENT0;
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &colorAttachment);
    glViewport(0, 0, _width, _height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram(_resolveProgram);
    glUniform2f(_sizeUniform, (GLfloat) _width, (GLfloat) _height);
    glUniform2f(_radiusUniform, _cornerRadiusXPx.load(), _cornerRadiusYPx.load());
    glUniform1f(_premultipliedUniform, _premultipliedAlpha.load() ? 1.0f : 0.0f);
    glBindVertexArray(_resolveVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, _textureId);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTexture(GL_TEXTURE_2D, _resolveTexture);
    glGenerateMipmap(GL_TEXTURE_2D);
}
