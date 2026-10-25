//
//  VROPanelGLStateGuard.h
//  ViroRenderer
//
//  Saves the GL state a panel copy pass touches and restores it on scope exit.
//  Used by the passes that run on Viro's own context between its frames: the
//  SurfaceTexture resolve copy (VROAndroidViewTexture) and the OpenXR
//  compositor finish pass (VROOpenXRRivePanelProducer).
//
//  Why restore instead of invalidating the driver cache: VRODriverOpenGL keeps a
//  CPU-side copy of GL state. invalidateGLStateCache() (VRODriverOpenGL.h)
//  forgets only texture bindings, the bound render target and the bound shader.
//  Depth read/write, colour masks, stencil, cull mode and blend mode stay
//  cached, so a pass that changed them and did not put them back would leave
//  the cache lying to the next draw. This guard puts every piece of state the
//  passes change back to its exact prior value, cached or not, so the cache is
//  still true afterwards and no invalidation is needed.
//
//  State covered: draw and read framebuffer bindings, viewport, current
//  program, vertex array, GL_ARRAY_BUFFER binding, active texture unit, the 2D
//  and external-OES bindings and the sampler on unit 0, colour write mask,
//  and the enables for blend, depth test, cull face, scissor test, stencil
//  test and (when EXT_sRGB_write_control exists) GL_FRAMEBUFFER_SRGB_EXT.
//  A pass must not change anything outside this list.
//

#pragma once

#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif

class VROPanelGLStateGuard {
public:
    // hasSrgbWriteControl: EXT_sRGB_write_control is supported, so
    // GL_FRAMEBUFFER_SRGB_EXT is a legal enable and is saved too.
    explicit VROPanelGLStateGuard(bool hasSrgbWriteControl = false)
        : _hasSrgbWriteControl(hasSrgbWriteControl) {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &_drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &_readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, _viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &_vertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &_arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &_activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &_texture2D);
        glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &_textureExternal);
        glGetIntegerv(GL_SAMPLER_BINDING, &_sampler);
        glGetBooleanv(GL_COLOR_WRITEMASK, _colorMask);
        _blend = glIsEnabled(GL_BLEND);
        _depthTest = glIsEnabled(GL_DEPTH_TEST);
        _cullFace = glIsEnabled(GL_CULL_FACE);
        _scissorTest = glIsEnabled(GL_SCISSOR_TEST);
        _stencilTest = glIsEnabled(GL_STENCIL_TEST);
        if (_hasSrgbWriteControl) {
            _framebufferSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
        }
    }

    ~VROPanelGLStateGuard() {
        if (_hasSrgbWriteControl) {
            setEnabled(GL_FRAMEBUFFER_SRGB_EXT, _framebufferSrgb);
        }
        setEnabled(GL_BLEND, _blend);
        setEnabled(GL_DEPTH_TEST, _depthTest);
        setEnabled(GL_CULL_FACE, _cullFace);
        setEnabled(GL_SCISSOR_TEST, _scissorTest);
        setEnabled(GL_STENCIL_TEST, _stencilTest);
        glColorMask(_colorMask[0], _colorMask[1], _colorMask[2], _colorMask[3]);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, (GLuint) _sampler);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint) _textureExternal);
        glBindTexture(GL_TEXTURE_2D, (GLuint) _texture2D);
        glActiveTexture((GLenum) _activeTexture);
        glBindVertexArray((GLuint) _vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint) _arrayBuffer);
        glUseProgram((GLuint) _program);
        glViewport(_viewport[0], _viewport[1], _viewport[2], _viewport[3]);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint) _readFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint) _drawFramebuffer);
    }

    VROPanelGLStateGuard(const VROPanelGLStateGuard &) = delete;
    VROPanelGLStateGuard &operator=(const VROPanelGLStateGuard &) = delete;

private:
    static void setEnabled(GLenum cap, GLboolean enabled) {
        if (enabled) {
            glEnable(cap);
        } else {
            glDisable(cap);
        }
    }

    bool _hasSrgbWriteControl;
    GLint _drawFramebuffer = 0, _readFramebuffer = 0;
    GLint _viewport[4] = { 0, 0, 0, 0 };
    GLint _program = 0, _vertexArray = 0, _arrayBuffer = 0;
    GLint _activeTexture = GL_TEXTURE0;
    GLint _texture2D = 0, _textureExternal = 0, _sampler = 0;
    GLboolean _colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLboolean _blend = GL_FALSE, _depthTest = GL_FALSE, _cullFace = GL_FALSE;
    GLboolean _scissorTest = GL_FALSE, _stencilTest = GL_FALSE;
    GLboolean _framebufferSrgb = GL_FALSE;
};
