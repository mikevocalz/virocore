//
//  VROOpenXRRivePanelFallback.cpp
//  ViroRenderer
//

#include "VROOpenXRRivePanelFallback.h"

#include "VRODriverOpenGL.h"
#include "VROLog.h"
#include "VROPanelGLStateGuard.h"
#include "VROPanelShaderSources.h"
#include "VROTextureSubstrateOpenGL.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace {

bool hasGLExtension(const char *name) {
    const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (extensions == nullptr) return false;
    const size_t length = std::strlen(name);
    for (const char *p = extensions; (p = std::strstr(p, name)) != nullptr; p += length) {
        if ((p == extensions || p[-1] == ' ') && (p[length] == ' ' || p[length] == '\0')) {
            return true;
        }
    }
    return false;
}

GLuint compile(GLenum type, const std::string &source) {
    GLuint shader = glCreateShader(type);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        perr("VROOpenXRRivePanelFallback: shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

VROOpenXRRivePanelFallback::VROOpenXRRivePanelFallback(std::shared_ptr<VRORivePanel> panel,
                                                       int widthPx, int heightPx,
                                                       VROPanelCornerRadii radiiPx)
    : VROTexture(VROTextureType::Texture2D, VROTextureInternalFormat::RGBA8, VROStereoMode::None),
      _panel(std::move(panel)), _widthPx(widthPx), _heightPx(heightPx), _radii(radiiPx) {}

VROOpenXRRivePanelFallback::~VROOpenXRRivePanelFallback() {
    std::shared_ptr<VRODriverOpenGL> driver = _driver.lock();
    if (!driver) return;
    // The substrate owns _texture once init() succeeded.
    if (_texture != 0 && !_ready) driver->deleteTexture(_texture);
    if (_framebuffer != 0) driver->deleteFramebuffer(_framebuffer);
    if (_program != 0) driver->deleteProgram(_program);
    if (_vertexArray != 0) driver->deleteVertexArray(_vertexArray);
    if (_vertexBuffer != 0) driver->deleteBuffer(_vertexBuffer);
}

bool VROOpenXRRivePanelFallback::init(std::shared_ptr<VRODriverOpenGL> driver) {
    if (!driver || _panel == nullptr || _widthPx <= 0 || _heightPx <= 0) return false;
    _driver = driver;
    VROPanelGLStateGuard guard;
    _hasSrgbWriteControl = hasGLExtension("GL_EXT_sRGB_write_control");
    const bool sRGBStorage = driver->isLinearRenderingEnabled();

    const GLsizei levels =
        (GLsizei) std::floor(std::log2((double) std::max(_widthPx, _heightPx))) + 1;
    glGenTextures(1, &_texture);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexStorage2D(GL_TEXTURE_2D, levels, sRGBStorage ? GL_SRGB8_ALPHA8 : GL_RGBA8, _widthPx,
                   _heightPx);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        perr("VROOpenXRRivePanelFallback: framebuffer incomplete (0x%x)", status);
        return false;
    }
    // Transparent until the first copy.
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, _widthPx, _heightPx);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Same program as the finish pass: linear output exactly when the
    // destination encodes on write.
    GLuint vertexShader = compile(GL_VERTEX_SHADER, kVROPanelCopyVertexShader);
    GLuint fragmentShader = compile(GL_FRAGMENT_SHADER, VROPanelCopyFragmentShader(sRGBStorage));
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
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
        perr("VROOpenXRRivePanelFallback: program link failed");
        return false;
    }
    glUseProgram(_program);
    glUniform1i(glGetUniformLocation(_program, "u_source"), 0);
    _sizeUniform = glGetUniformLocation(_program, "u_size");
    _radiusUniform = glGetUniformLocation(_program, "u_radius");

    const GLfloat triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    glGenVertexArrays(1, &_vertexArray);
    glBindVertexArray(_vertexArray);
    glGenBuffers(1, &_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    std::unique_ptr<VROTextureSubstrate> substrate(
        new VROTextureSubstrateOpenGL(GL_TEXTURE_2D, _texture, driver, true));
    setSubstrate(0, std::move(substrate));
    _ready = true;
    pinfo("VROOpenXRRivePanelFallback: %dx%d, %d mip levels, %s storage", _widthPx, _heightPx,
          (int) levels, sRGBStorage ? "sRGB" : "RGBA8");
    return true;
}

bool VROOpenXRRivePanelFallback::update() {
    if (!_ready) return false;
    VRORivePanelFrame frame;
    if (!_panel->beginFrameRead(&frame)) return false;
    {
        VROPanelGLStateGuard guard(_hasSrgbWriteControl);
        glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        const GLenum attachment = GL_COLOR_ATTACHMENT0;
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &attachment);
        glViewport(0, 0, _widthPx, _heightPx);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (_hasSrgbWriteControl) glEnable(GL_FRAMEBUFFER_SRGB_EXT);
        glUseProgram(_program);
        glUniform2f(_sizeUniform, (GLfloat) _widthPx, (GLfloat) _heightPx);
        glUniform2f(_radiusUniform, _radii.x, _radii.y);
        glBindVertexArray(_vertexArray);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, 0);
        glBindTexture(GL_TEXTURE_2D, frame.texture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, _texture);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    _panel->endFrameRead();
    return true;
}
