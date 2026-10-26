//
//  VRORivePanelEGL.cpp
//  ViroRenderer
//

#include "VRORivePanelEGL.h"

#include <EGL/eglext.h>
#include <android/log.h>
#include <android/native_window.h>

#include <cstring>
#include <vector>

#define RIVE_PANEL_LOG_TAG "ViroRivePanel"

namespace {

const char *eglErrorName(EGLint error) {
    switch (error) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
        default: return "EGL_UNKNOWN_ERROR";
    }
}

bool hasExtension(const char *extensions, const char *name) {
    if (extensions == nullptr) return false;
    const size_t length = std::strlen(name);
    for (const char *p = extensions; (p = std::strstr(p, name)) != nullptr; p += length) {
        const bool startOk = p == extensions || p[-1] == ' ';
        const bool endOk = p[length] == ' ' || p[length] == '\0';
        if (startOk && endOk) return true;
    }
    return false;
}

EGLint configAttrib(EGLDisplay display, EGLConfig config, EGLint attribute) {
    EGLint value = 0;
    eglGetConfigAttrib(display, config, attribute, &value);
    return value;
}

// Exactly 8 bits per channel: eglChooseConfig sorts deeper colour (10-bit)
// first, which would change the Surface's pixel format.
EGLConfig chooseConfig(EGLDisplay display, EGLint depth, EGLint stencil) {
    const EGLint attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, depth, EGL_STENCIL_SIZE, stencil,
        EGL_NONE,
    };
    EGLint count = 0;
    if (!eglChooseConfig(display, attributes, nullptr, 0, &count) || count <= 0) {
        return nullptr;
    }
    std::vector<EGLConfig> configs(static_cast<size_t>(count));
    if (!eglChooseConfig(display, attributes, configs.data(), count, &count)) {
        return nullptr;
    }
    for (EGLint i = 0; i < count; ++i) {
        EGLConfig c = configs[static_cast<size_t>(i)];
        if (configAttrib(display, c, EGL_RED_SIZE) == 8 &&
            configAttrib(display, c, EGL_GREEN_SIZE) == 8 &&
            configAttrib(display, c, EGL_BLUE_SIZE) == 8 &&
            configAttrib(display, c, EGL_ALPHA_SIZE) == 8) {
            return c;
        }
    }
    return nullptr;
}

} // namespace

VRORivePanelWindowRef::VRORivePanelWindowRef(ANativeWindow *window) : _window(window) {
    if (_window != nullptr) ANativeWindow_acquire(_window);
}

VRORivePanelWindowRef::~VRORivePanelWindowRef() { reset(); }

VRORivePanelWindowRef::VRORivePanelWindowRef(VRORivePanelWindowRef &&other) noexcept
    : _window(other._window) {
    other._window = nullptr;
}

VRORivePanelWindowRef &VRORivePanelWindowRef::operator=(VRORivePanelWindowRef &&other) noexcept {
    if (this != &other) {
        reset();
        _window = other._window;
        other._window = nullptr;
    }
    return *this;
}

void VRORivePanelWindowRef::reset() {
    if (_window != nullptr) {
        ANativeWindow_release(_window);
        _window = nullptr;
    }
}

void VRORivePanelEGL::recordError(const char *call) {
    const EGLint error = eglGetError();
    _lastError = std::string(call) + ": " + eglErrorName(error);
    __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "%s", _lastError.c_str());
}

std::unique_ptr<VRORivePanelEGL> VRORivePanelEGL::create(std::string *error) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        *error = std::string("eglGetDisplay: ") + eglErrorName(eglGetError());
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "%s", error->c_str());
        return nullptr;
    }
    return createOn(display, EGL_NO_CONTEXT, error);
}

std::unique_ptr<VRORivePanelEGL> VRORivePanelEGL::createShared(EGLDisplay display,
                                                               EGLContext shareContext,
                                                               std::string *error) {
    if (display == EGL_NO_DISPLAY || shareContext == EGL_NO_CONTEXT) {
        *error = "createShared: no display or share context";
        return nullptr;
    }
    return createOn(display, shareContext, error);
}

std::unique_ptr<VRORivePanelEGL> VRORivePanelEGL::createOn(EGLDisplay display,
                                                           EGLContext shareContext,
                                                           std::string *error) {
    std::unique_ptr<VRORivePanelEGL> egl(new VRORivePanelEGL());
    egl->_display = display;
    // Already initialised by Viro in the shared case; eglInitialize on an
    // initialised display only returns its version.
    if (!eglInitialize(egl->_display, nullptr, nullptr)) {
        egl->recordError("eglInitialize");
        *error = egl->_lastError;
        egl->_display = EGL_NO_DISPLAY;
        return nullptr;
    }

    // Depth/stencil matter only if Rive falls back to depthStencil mode on
    // the window's framebuffer (FramebufferRenderTargetGL warns below 24/8).
    egl->_config = chooseConfig(egl->_display, 24, 8);
    if (egl->_config == nullptr) {
        egl->_config = chooseConfig(egl->_display, 0, 0);
    }
    if (egl->_config == nullptr) {
        egl->recordError("eglChooseConfig (RGBA8, ES3, window+pbuffer)");
        *error = egl->_lastError;
        return nullptr;
    }

    const char *extensions = eglQueryString(egl->_display, EGL_EXTENSIONS);
    egl->_hasColorspaceExtension = hasExtension(extensions, "EGL_KHR_gl_colorspace");

    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    // A share context only has to be on the same display and the same client
    // API (EGL 1.5 section 3.7.1); the configs need not match.
    egl->_context =
        eglCreateContext(egl->_display, egl->_config, shareContext, contextAttributes);
    if (egl->_context == EGL_NO_CONTEXT) {
        egl->recordError(shareContext == EGL_NO_CONTEXT ? "eglCreateContext (ES 3)"
                                                        : "eglCreateContext (ES 3, shared)");
        *error = egl->_lastError;
        return nullptr;
    }

    const EGLint pbufferAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    egl->_pbuffer = eglCreatePbufferSurface(egl->_display, egl->_config, pbufferAttributes);
    if (egl->_pbuffer == EGL_NO_SURFACE) {
        egl->recordError("eglCreatePbufferSurface");
        *error = egl->_lastError;
        return nullptr;
    }
    if (!egl->makeCurrent(egl->_pbuffer)) {
        *error = egl->_lastError;
        return nullptr;
    }
    __android_log_print(ANDROID_LOG_INFO, RIVE_PANEL_LOG_TAG,
                        "EGL ready%s: RGBA8 depth %d stencil %d samples %d, colorspace ext %d",
                        shareContext == EGL_NO_CONTEXT ? "" : " (shared with XR context)",
                        configAttrib(egl->_display, egl->_config, EGL_DEPTH_SIZE),
                        configAttrib(egl->_display, egl->_config, EGL_STENCIL_SIZE),
                        configAttrib(egl->_display, egl->_config, EGL_SAMPLES),
                        egl->_hasColorspaceExtension ? 1 : 0);
    return egl;
}

VRORivePanelEGL::~VRORivePanelEGL() {
    if (_display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (_windowSurface != EGL_NO_SURFACE) {
        eglDestroySurface(_display, _windowSurface);
        _windowSurface = EGL_NO_SURFACE;
    }
    _window.reset();
    if (_pbuffer != EGL_NO_SURFACE) {
        eglDestroySurface(_display, _pbuffer);
        _pbuffer = EGL_NO_SURFACE;
    }
    if (_context != EGL_NO_CONTEXT) {
        eglDestroyContext(_display, _context);
        _context = EGL_NO_CONTEXT;
    }
}

bool VRORivePanelEGL::makeCurrent(EGLSurface surface) {
    if (!eglMakeCurrent(_display, surface, surface, _context)) {
        recordError("eglMakeCurrent");
        return false;
    }
    return true;
}

bool VRORivePanelEGL::attachWindow(VRORivePanelWindowRef window, int widthPx, int heightPx,
                                   std::string *error) {
    detachWindow();
    if (!window) {
        *error = "attachWindow: null window";
        return false;
    }
    // WINDOW_FORMAT_RGBA_8888 matches the config. An explicit size makes the
    // producer's buffers match the render target whatever default size the
    // consumer's SurfaceTexture was given.
    const int32_t status =
        ANativeWindow_setBuffersGeometry(window.get(), widthPx, heightPx, WINDOW_FORMAT_RGBA_8888);
    if (status != 0) {
        *error = "ANativeWindow_setBuffersGeometry failed: " + std::to_string(status);
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "%s", error->c_str());
        return false;
    }

    const EGLint linearAttributes[] = {EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_LINEAR_KHR,
                                       EGL_NONE};
    const EGLint *attributes = _hasColorspaceExtension ? linearAttributes : nullptr;
    EGLSurface surface = eglCreateWindowSurface(
        _display, _config, reinterpret_cast<EGLNativeWindowType>(window.get()), attributes);
    if (surface == EGL_NO_SURFACE) {
        recordError("eglCreateWindowSurface");
        *error = _lastError;
        return false;
    }
    if (!makeCurrent(surface)) {
        *error = _lastError;
        eglDestroySurface(_display, surface);
        makeCurrent(_pbuffer);
        return false;
    }
    _windowSurface = surface;
    _window = std::move(window);

    // Interval 0 puts the BufferQueue producer in async mode: a queued frame
    // the consumer has not latched yet is replaced, and eglSwapBuffers never
    // blocks on Viro. Pacing comes from the worker's own frame clock, so a
    // stalled consumer cannot wedge the worker (and with it surfaceLost or
    // dispose).
    if (!eglSwapInterval(_display, 0)) {
        recordError("eglSwapInterval(0)");
    }
    return true;
}

void VRORivePanelEGL::detachWindow() {
    if (_windowSurface == EGL_NO_SURFACE) {
        _window.reset();
        return;
    }
    makeCurrent(_pbuffer);
    eglDestroySurface(_display, _windowSurface);
    _windowSurface = EGL_NO_SURFACE;
    // Only after the EGL surface is gone: it holds the window's producer
    // connection until destroyed.
    _window.reset();
}

bool VRORivePanelEGL::resizeWindow(int widthPx, int heightPx, std::string *error) {
    if (!_window) return true;
    const int32_t status =
        ANativeWindow_setBuffersGeometry(_window.get(), widthPx, heightPx, WINDOW_FORMAT_RGBA_8888);
    if (status != 0) {
        *error = "ANativeWindow_setBuffersGeometry failed: " + std::to_string(status);
        return false;
    }
    return true;
}

VRORivePanelEGL::SwapResult VRORivePanelEGL::swapBuffers() {
    if (_windowSurface == EGL_NO_SURFACE) return SwapResult::Failed;
    if (eglSwapBuffers(_display, _windowSurface)) return SwapResult::Ok;
    const EGLint error = eglGetError();
    _lastError = std::string("eglSwapBuffers: ") + eglErrorName(error);
    __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "%s", _lastError.c_str());
    switch (error) {
        case EGL_BAD_SURFACE:
        case EGL_BAD_NATIVE_WINDOW:
            return SwapResult::SurfaceLost;
        case EGL_CONTEXT_LOST:
            return SwapResult::ContextLost;
        default:
            return SwapResult::Failed;
    }
}
