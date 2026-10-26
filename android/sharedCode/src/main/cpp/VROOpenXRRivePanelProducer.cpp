//
//  VROOpenXRRivePanelProducer.cpp
//  ViroRenderer
//

#include "VROOpenXRRivePanelProducer.h"

#include "VROPanelGLStateGuard.h"
#include "VROPanelShaderSources.h"

#include <android/log.h>

#include <cstring>
#include <utility>

#define XR_RIVE_LOG_TAG "ViroOpenXRRivePanel"

namespace {

bool hasGLExtension(const char *name) {
    const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (extensions == nullptr) return false;
    const size_t length = std::strlen(name);
    for (const char *p = extensions; (p = std::strstr(p, name)) != nullptr; p += length) {
        const bool startOk = p == extensions || p[-1] == ' ';
        const bool endOk = p[length] == ' ' || p[length] == '\0';
        if (startOk && endOk) return true;
    }
    return false;
}

GLuint compile(GLenum type, const std::string &source, std::string *error) {
    GLuint shader = glCreateShader(type);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        *error = std::string("finish pass shader compile failed: ") + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

std::shared_ptr<VROOpenXRRivePanelProducer> VROOpenXRRivePanelProducer::create(
    std::shared_ptr<VRORivePanel> panel, int64_t swapchainFormat, VROPanelCornerRadii radiiPx,
    std::string *error) {
    if (panel == nullptr || !panel->isTextureRing()) {
        *error = "finish pass needs a VRORivePanel in texture-ring mode";
        return nullptr;
    }
    std::shared_ptr<VROOpenXRRivePanelProducer> producer(new VROOpenXRRivePanelProducer());
    producer->_panel = std::move(panel);
    producer->_radii = radiiPx;
    // GL_SRGB8_ALPHA8 == GL_SRGB8_ALPHA8_EXT (0x8C43).
    producer->_linearOutput = swapchainFormat == GL_SRGB8_ALPHA8;
    producer->_context = eglGetCurrentContext();
    if (producer->_context == EGL_NO_CONTEXT) {
        *error = "finish pass created without a current GL context";
        return nullptr;
    }
    if (!producer->createGLResources(error)) return nullptr;
    __android_log_print(ANDROID_LOG_INFO, XR_RIVE_LOG_TAG,
                        "finish pass ready: %s output, corner radii %.2f x %.2f px",
                        producer->_linearOutput ? "linear (sRGB swapchain)" : "encoded (RGBA8)",
                        radiiPx.x, radiiPx.y);
    return producer;
}

bool VROOpenXRRivePanelProducer::createGLResources(std::string *error) {
    // Every binding below is restored on return.
    VROPanelGLStateGuard guard;
    _hasSrgbWriteControl = hasGLExtension("GL_EXT_sRGB_write_control");

    GLuint vertexShader = compile(GL_VERTEX_SHADER, kVROPanelCopyVertexShader, error);
    if (vertexShader == 0) return false;
    GLuint fragmentShader =
        compile(GL_FRAGMENT_SHADER, VROPanelCopyFragmentShader(_linearOutput), error);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return false;
    }
    _program = glCreateProgram();
    glAttachShader(_program, vertexShader);
    glAttachShader(_program, fragmentShader);
    glLinkProgram(_program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    GLint linked = GL_FALSE;
    glGetProgramiv(_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512] = {0};
        glGetProgramInfoLog(_program, sizeof(log), nullptr, log);
        *error = std::string("finish pass program link failed: ") + log;
        glDeleteProgram(_program);
        _program = 0;
        return false;
    }
    glUseProgram(_program);
    glUniform1i(glGetUniformLocation(_program, "u_source"), 0);
    _sizeUniform = glGetUniformLocation(_program, "u_size");
    _radiusUniform = glGetUniformLocation(_program, "u_radius");

    // One triangle that covers the target.
    const GLfloat triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    glGenVertexArrays(1, &_vertexArray);
    glBindVertexArray(_vertexArray);
    glGenBuffers(1, &_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenFramebuffers(1, &_framebuffer);
    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        *error = "finish pass resource creation failed: GL error " + std::to_string(glError);
        return false;
    }
    return true;
}

VROOpenXRRivePanelProducer::~VROOpenXRRivePanelProducer() {
    // At session teardown the XR context is no longer current; the objects
    // are then freed with the context instead.
    if (_context != EGL_NO_CONTEXT && eglGetCurrentContext() == _context) {
        if (_framebuffer != 0) glDeleteFramebuffers(1, &_framebuffer);
        if (_vertexBuffer != 0) glDeleteBuffers(1, &_vertexBuffer);
        if (_vertexArray != 0) glDeleteVertexArrays(1, &_vertexArray);
        if (_program != 0) glDeleteProgram(_program);
    }
    // Last: if this is the final reference, dispose() joins the Rive worker,
    // which deletes the ring textures and its shared context after the
    // compositor has finished with them.
    _panel.reset();
}

VROOpenXRPanelProducer VROOpenXRRivePanelProducer::bind(
    const std::shared_ptr<VROOpenXRRivePanelProducer> &producer) {
    std::shared_ptr<VROOpenXRRivePanelProducer> owned = producer;
    return [owned](GLuint image, uint32_t widthPx, uint32_t heightPx) {
        return owned->draw(image, widthPx, heightPx);
    };
}

bool VROOpenXRRivePanelProducer::draw(GLuint image, uint32_t widthPx, uint32_t heightPx) {
    if (_program == 0 || image == 0) return false;
    VRORivePanelFrame frame;
    if (!_panel->beginFrameRead(&frame)) {
        return false;  // nothing published yet; the compositor retries next frame
    }

    bool drawn = false;
    {
        VROPanelGLStateGuard guard(_hasSrgbWriteControl);
        glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image, 0);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status == GL_FRAMEBUFFER_COMPLETE) {
            // Every texel is overwritten, so the previous contents need not load.
            const GLenum attachment = GL_COLOR_ATTACHMENT0;
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &attachment);
            glViewport(0, 0, (GLsizei) widthPx, (GLsizei) heightPx);
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            if (_hasSrgbWriteControl) {
                // Linear output relies on the sRGB encode at write; RGBA8
                // swapchains have no encode either way.
                glEnable(GL_FRAMEBUFFER_SRGB_EXT);
            }
            glUseProgram(_program);
            glUniform2f(_sizeUniform, (GLfloat) widthPx, (GLfloat) heightPx);
            glUniform2f(_radiusUniform, _radii.x, _radii.y);
            glBindVertexArray(_vertexArray);
            glActiveTexture(GL_TEXTURE0);
            glBindSampler(0, 0);
            glBindTexture(GL_TEXTURE_2D, frame.texture);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            drawn = true;
        } else {
            __android_log_print(ANDROID_LOG_ERROR, XR_RIVE_LOG_TAG,
                                "swapchain image %u framebuffer incomplete (0x%x)", image, status);
        }
        // Hold no attachment to a swapchain image between frames: the runtime
        // owns it and destroys it with the swapchain.
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }
    // After the sampling draw, on this thread: fences it for the worker.
    _panel->endFrameRead();
    return drawn;
}
