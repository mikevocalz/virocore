//
//  VRORivePanelEGL.h
//  ViroRenderer
//
//  EGL objects owned by one VRORivePanel worker thread: a GLES 3 context of
//  its own (never shared with Viro's), a 1x1 pbuffer to stay current on while
//  no window is attached, and the window surface on the panel's ANativeWindow.
//  Every method must be called on the worker thread that created the object.
//
//  Colour: the config is RGBA8 and the window surface is created with
//  EGL_GL_COLORSPACE_LINEAR_KHR (when the extension exists; linear is also
//  EGL's default), so no sRGB encode happens on write. Rive's GL backend
//  already writes sRGB-encoded bytes into UNORM targets, and the scene-panel
//  route samples the Surface as sRGB-encoded bytes (ADR 0001 D6). An sRGB
//  window surface would encode twice.
//

#pragma once

#include <EGL/egl.h>

#include <memory>
#include <string>

struct ANativeWindow;

// Owns one ANativeWindow reference (ANativeWindow_acquire / _release).
class __attribute__((visibility("hidden"))) VRORivePanelWindowRef {
public:
    VRORivePanelWindowRef() = default;
    // Acquires a new reference; null stays empty.
    explicit VRORivePanelWindowRef(ANativeWindow *window);
    ~VRORivePanelWindowRef();
    VRORivePanelWindowRef(VRORivePanelWindowRef &&other) noexcept;
    VRORivePanelWindowRef &operator=(VRORivePanelWindowRef &&other) noexcept;
    VRORivePanelWindowRef(const VRORivePanelWindowRef &) = delete;
    VRORivePanelWindowRef &operator=(const VRORivePanelWindowRef &) = delete;

    ANativeWindow *get() const { return _window; }
    explicit operator bool() const { return _window != nullptr; }
    void reset();

private:
    ANativeWindow *_window = nullptr;
};

class __attribute__((visibility("hidden"))) VRORivePanelEGL {
public:
    enum class SwapResult { Ok, SurfaceLost, ContextLost, Failed };

    // Initializes the display, picks a config, creates the context and the
    // pbuffer, and makes them current. Null with *error set on failure.
    static std::unique_ptr<VRORivePanelEGL> create(std::string *error);

    // Releases current, destroys the window surface (before dropping the
    // window reference), the pbuffer and the context. Does not call
    // eglTerminate: the display is process-wide and Viro uses it too.
    ~VRORivePanelEGL();
    VRORivePanelEGL(const VRORivePanelEGL &) = delete;
    VRORivePanelEGL &operator=(const VRORivePanelEGL &) = delete;

    // Replaces any current window. Sets the window's buffer size to
    // widthPx x heightPx RGBA_8888, creates the window surface, makes it
    // current, and sets swap interval 0. On failure the pbuffer is current and
    // no window is held.
    bool attachWindow(VRORivePanelWindowRef window, int widthPx, int heightPx,
                      std::string *error);
    // Makes the pbuffer current, destroys the window surface, then releases
    // the window reference. No-op without a window.
    void detachWindow();
    // Resizes the window's buffers; takes effect at the next dequeue.
    bool resizeWindow(int widthPx, int heightPx, std::string *error);
    bool hasWindow() const { return _windowSurface != EGL_NO_SURFACE; }

    SwapResult swapBuffers();
    // eglGetError() name of the last failing call, for logs.
    const std::string &lastError() const { return _lastError; }

private:
    VRORivePanelEGL() = default;
    bool makeCurrent(EGLSurface surface);
    void recordError(const char *call);

    EGLDisplay _display = EGL_NO_DISPLAY;
    EGLConfig _config = nullptr;
    EGLContext _context = EGL_NO_CONTEXT;
    EGLSurface _pbuffer = EGL_NO_SURFACE;
    EGLSurface _windowSurface = EGL_NO_SURFACE;
    VRORivePanelWindowRef _window;
    bool _hasColorspaceExtension = false;
    std::string _lastError;
};
