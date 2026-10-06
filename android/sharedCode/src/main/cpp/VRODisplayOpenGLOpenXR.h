// VRODisplayOpenGLOpenXR.h
// ViroRenderer
//
// OpenXR display (render target) for one eye. Unlike OVR's ovrFramebuffer,
// OpenXR returns raw GL textures from the swapchain. We wrap each one in an
// FBO and bind it here when the eye render begins.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VRODISPLAYOPENGLOPENXR_H
#define ANDROID_VRODISPLAYOPENGLOPENXR_H

#include <memory>
#include <vector>
#include <unordered_map>
#include <GLES3/gl3.h>
#include <android/log.h>
#include "VROOpenGL.h"
#include "VRODisplayOpenGL.h"

#define XRDISPLAY_TAG "VRODisplayOpenXR"
#define XRDLOG(...) __android_log_print(ANDROID_LOG_VERBOSE, XRDISPLAY_TAG, __VA_ARGS__)
#define XRELOG(...) __android_log_print(ANDROID_LOG_ERROR,   XRDISPLAY_TAG, __VA_ARGS__)

class VRODriverOpenGL;

/*
 * Wraps one OpenXR swapchain image as a Viro render target. The renderer
 * creates one instance per eye. Before rendering, call setSwapchainImage()
 * with the GL texture ID returned by XrSwapchainImageOpenGLESKHR, then bind().
 */
class VRODisplayOpenGLOpenXR : public VRODisplayOpenGL {
public:

    VRODisplayOpenGLOpenXR(std::shared_ptr<VRODriverOpenGL> driver)
        : VRODisplayOpenGL(0, driver),
          _fbo(0),
          _colorTex(0) {
    }

    virtual ~VRODisplayOpenGLOpenXR() {
        destroyFramebuffer();
    }

    /*
     * Called once per frame, after xrAcquireSwapchainImage. Sets the GL
     * texture (from XrSwapchainImageOpenGLESKHR.image) and recreates the
     * FBO if the texture has changed.
     *
     * One display object serves BOTH eyes (VRODriverOpenGLAndroidOpenXR
     * returns a singleton), so _colorTex changes on every call and a
     * single-slot FBO would be destroyed and recreated twice per frame —
     * ~180 gen/alloc pairs a second, which stalls the GL pipeline and
     * presents as panel jitter even at full frame rate. Cache an
     * FBO+depth-RBO pair per swapchain texture instead: the pool is bounded
     * (3 images × 2 eyes) and each entry is created once per session.
     */
    void setSwapchainImage(GLuint colorTex, GLsizei width, GLsizei height) {
        auto it = _fbos.find(colorTex);
        if (it != _fbos.end()) {
            _fbo = it->second.fbo;
            _colorTex = colorTex;
            return;
        }

        GLuint fbo = 0, depthRbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // Try GL_TEXTURE_2D first; Quest may use GL_TEXTURE_2D_ARRAY even for arraySize=1
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            // Quest may use GL_TEXTURE_2D_ARRAY even for arraySize=1.
            // glFramebufferTextureLayer is GLES3.0 core — attach layer 0.
            XRELOG("GL_TEXTURE_2D attachment incomplete (0x%x), retrying as array layer 0", status);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorTex, 0, 0);
        }

        glGenRenderbuffers(1, &depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, depthRbo);

        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            XRELOG("OpenXR: FBO still incomplete after retry, status=0x%x  tex=%u  %dx%d",
                   status, colorTex, (int)width, (int)height);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteRenderbuffers(1, &depthRbo);
            glDeleteFramebuffers(1, &fbo);
            _fbo = 0;
            _colorTex = 0;
            return;  // leave no entry — retry cleanly next frame
        }

        _fbos[colorTex] = { fbo, depthRbo };
        _fbo = fbo;
        _colorTex = colorTex;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // For mixed-reality (passthrough) the swapchain must be cleared TRANSPARENT
    // (alpha 0) so empty regions reveal the passthrough layer beneath. For fully
    // virtual VR it must be OPAQUE (alpha 1) so the background is solid black.
    // The renderer sets this when passthrough is toggled.
    void setClearAlpha(float alpha) { _clearAlpha = alpha; }

    void bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glViewport(_viewport.getX(), _viewport.getY(),
                   _viewport.getWidth(), _viewport.getHeight());
        glScissor(_viewport.getX(), _viewport.getY(),
                  _viewport.getWidth(), _viewport.getHeight());
        // Force the colour write mask fully on (incl. alpha) — Viro's cached state
        // can leave the alpha channel masked off, which would skip the alpha clear.
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, _clearAlpha);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

private:

    struct FboEntry {
        GLuint fbo;
        GLuint depthRbo;
    };

    // FBO + depth RBO per swapchain texture, created once and reused for the
    // session — see setSwapchainImage for why a single slot is not enough.
    std::unordered_map<GLuint, FboEntry> _fbos;
    GLuint _fbo;       // currently active entry (set by setSwapchainImage)
    GLuint _colorTex;  // texture the active entry is bound to
    float  _clearAlpha = 1.0f;  // 0 for MR/passthrough, 1 for opaque VR

    void destroyFramebuffer() {
        for (auto &kv : _fbos) {
            glDeleteFramebuffers(1, &kv.second.fbo);
            glDeleteRenderbuffers(1, &kv.second.depthRbo);
        }
        _fbos.clear();
        _fbo = 0;
        _colorTex = 0;
    }
};

#endif  // ANDROID_VRODISPLAYOPENGLOPENXR_H
