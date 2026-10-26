//
//  VRORivePanel.h
//  ViroRenderer
//
//  Native Rive producer for scene panels (viro-external ADR 0001, route B1).
//  Renders one .riv state machine into the Android Surface that Viro hands out
//  through CanvasSourceBridge. The caller converts that Surface to an
//  ANativeWindow (ANativeWindow_fromSurface) and passes it here.
//
//  Stable exported API of libviro_renderer.so for packages that link it as an
//  imported library (the Nitro HybridObjects in @viro-external/native-panels).
//  No rive-runtime type appears here; rive is linked with hidden visibility.
//
//  Threading: every method may be called from any thread, including from the
//  status listener (see StatusListener for the two that then do not block).
//  Calls enqueue a command for
//  the panel's worker thread, which owns all Rive objects, its own EGL context
//  and the EGL window surface. Nothing is shared with Viro's GL context; the
//  Surface's buffer queue is the hand-off.
//
//  Output: premultiplied alpha, sRGB-encoded bytes in an RGBA8 (non-sRGB)
//  window surface, cleared to transparent. This is the scene-panel contract
//  in ADR 0001 D6.
//
//  Corners (ADR 0001 "Corner ownership"): the producer draws square and the
//  consumer rounds with the shared coverage function (VROPanelCoverage.h) in
//  the scene resolve copy, the compositor finish pass and the scene-mesh
//  fallback. Pass the consumer's radii as coverageRadiusXPx/YPx so pointer
//  hits follow the drawn corners. cornerRadiusPx makes the producer clip its
//  own corners and exists only for displays that do not round at the
//  consumer; with Viro leave it 0.
//
//  Texture-ring mode (route B2, createTextureRing): the worker's EGL context
//  shares the OpenXR render context and renders into three GL_RGBA8 textures
//  instead of a window. The XR render thread reads the newest one through
//  beginFrameRead()/endFrameRead(); fences order the two in both directions
//  (VRORivePanelRing.h). FrameListener fires on the worker after each
//  published frame, which happens only while the state machine advances or
//  after input, data or lifecycle changes, so the host can invalidate the
//  compositor panel exactly then. Same premultiplied, sRGB-encoded,
//  square-cornered pixels as the window mode.
//
//  Availability: arm64-v8a builds with VIRO_WITH_RIVE. Elsewhere isAvailable()
//  is false and create() returns null.
//

#pragma once

#include <EGL/egl.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#define VRO_RIVE_PANEL_API __attribute__((visibility("default")))

struct ANativeWindow;

// Mirrors rive::Fit without Fit::layout, which resizes the artboard to the
// frame and is not a panel mode.
enum class VRORivePanelFit : int32_t {
    Fill = 0,
    Contain = 1,
    Cover = 2,
    FitWidth = 3,
    FitHeight = 4,
    None = 5,
    ScaleDown = 6,
};

enum class VRORivePanelState : int32_t {
    // Worker thread is creating its EGL context and Rive render context.
    Initializing = 0,
    // Ready, no window surface attached. Commands are accepted and applied.
    WaitingForSurface = 1,
    // Window surface attached and rendering on demand.
    Active = 2,
    // Window surface attached, setActive(false): no frames are produced.
    Suspended = 3,
    // EGL or Rive failed (surface abandoned, context lost, render error).
    // attachSurface() retries from scratch, re-importing the last loaded file.
    Lost = 4,
    // dispose() ran. Terminal; every further call is rejected.
    Disposed = 5,
};

VRO_RIVE_PANEL_API const char *VRORivePanelStateName(VRORivePanelState state);

struct VRORivePanelOptions {
    // Producer-side clip radius in panel pixels, clamped to half the shorter
    // side. Default 0: the producer draws square and the Viro consumer rounds.
    // Set it only when the panel is shown somewhere that does not apply the
    // shared coverage function; with Viro it would round twice.
    float cornerRadiusPx = 0.0f;
    // Per-axis corner radii, in panel pixels, that the consumer's coverage
    // function applies (VROPanelCornerRadiiPx(radiusM, widthM, heightM,
    // widthPx, heightPx)). Used only for pointer hit testing: a press inside a
    // transparent corner does not reach Rive. When both are 0, hits are tested
    // against cornerRadiusPx instead.
    float coverageRadiusXPx = 0.0f;
    float coverageRadiusYPx = 0.0f;
    VRORivePanelFit fit = VRORivePanelFit::Contain;
    // rive::Alignment: -1 left/top, 0 centre, 1 right/bottom.
    float alignmentX = 0.0f;
    float alignmentY = 0.0f;
    // Background fill inside the rounded rect, drawn before the artboard, as
    // a straight (unpremultiplied) 0xAARRGGBB like rive::ColorInt. Alpha 0,
    // the default, draws nothing: the frame is cleared to transparent either
    // way, so corners and letterbox areas show the scene.
    uint32_t clearColor = 0x00000000u;
    // Upper bound on frames per second while the state machine is animating.
    // Settled content produces no frames at all. Match the display rate.
    float maxFrameRateHz = 72.0f;
};

// One published texture-ring frame, valid between beginFrameRead() and
// endFrameRead() on the reading thread.
struct VRORivePanelFrame {
    uint32_t texture = 0;  // GL_TEXTURE_2D name in the shared context group
    int32_t width = 0;
    int32_t height = 0;
    uint64_t sequence = 0;  // 1 for the first published frame, then +1
};

class VRO_RIVE_PANEL_API VRORivePanel {
public:
    // Called on the worker thread on every state change, and with the current
    // state plus a non-empty detail when a command fails without changing the
    // state (bad .riv, unknown artboard or property). Do not call dispose() or
    // surfaceLost() expecting them to block from inside the listener; they
    // return immediately there and finish after the listener returns.
    using StatusListener =
        std::function<void(VRORivePanelState state, const std::string &detail)>;

    // Texture-ring mode: called on the worker thread after each frame is
    // published, with its sequence number. Must not block or call back into
    // the panel; post the work elsewhere.
    using FrameListener = std::function<void(uint64_t sequence)>;

    static bool isAvailable();

    // window may be null; attach one later with attachSurface(). The panel
    // takes its own reference on the window (ANativeWindow_acquire), so the
    // caller may release its reference after this returns. Returns null when
    // Rive is not built for this ABI, sizes are outside 1..8192, or the worker
    // thread cannot start.
    static std::shared_ptr<VRORivePanel> create(ANativeWindow *window,
                                                int widthPx, int heightPx,
                                                VRORivePanelOptions options,
                                                StatusListener listener = nullptr);

    // Texture-ring mode. display and shareContext are the OpenXR renderer's
    // (VROSceneRendererOpenXR::getEGLDisplay/getEGLContext); the worker
    // creates its own GLES 3 context sharing shareContext's object namespace,
    // plus a 1x1 pbuffer. attachSurface() and surfaceLost() return false in
    // this mode; there is no window. After a GPU failure (state Lost),
    // setActive(true) rebuilds the context and the ring and re-imports the
    // file. Returns null under the same conditions as create().
    static std::shared_ptr<VRORivePanel> createTextureRing(EGLDisplay display,
                                                           EGLContext shareContext,
                                                           int widthPx, int heightPx,
                                                           VRORivePanelOptions options,
                                                           StatusListener listener = nullptr,
                                                           FrameListener frameListener = nullptr);

    ~VRORivePanel();
    VRORivePanel(const VRORivePanel &) = delete;
    VRORivePanel &operator=(const VRORivePanel &) = delete;

    VRORivePanelState getState() const;

    // Copies the bytes (up to 64 MiB) and imports them on the worker thread.
    // Replaces any previous file. The artboard and state machine named by the
    // latest select*() calls are instantiated, or the file's defaults when
    // none were named. Returns false if rejected before queueing.
    bool loadFile(const uint8_t *bytes, size_t length);
    // Empty name selects the default artboard / state machine.
    bool selectArtboard(const std::string &name);
    bool selectStateMachine(const std::string &name);

    // View-model writes by property path on the artboard's default view-model
    // instance. Queued and applied together on the worker thread before the
    // next advance; each write requests one frame.
    bool setNumber(const std::string &path, float value);
    bool setBoolean(const std::string &path, bool value);
    bool setString(const std::string &path, const std::string &value);
    bool fireTrigger(const std::string &path);

    // Panel pixel coordinates, origin top-left. Mapped into artboard space
    // with the same fit and alignment Rive renders with. A press that starts
    // in a transparent corner (coverage < 0.5) is dropped; a press that started inside keeps
    // receiving move/up even if it slides into a corner.
    bool pointerDown(float xPx, float yPx, int pointerId);
    bool pointerMove(float xPx, float yPx, int pointerId);
    bool pointerUp(float xPx, float yPx, int pointerId);

    bool setActive(bool active);
    bool resize(int widthPx, int heightPx);

    // Blocks until the worker has destroyed its EGL window surface and dropped
    // its window reference, so the caller can release the Surface right after.
    // File, artboard and state machine are kept.
    bool surfaceLost();
    // Replaces the current window, if any. Recovers from Lost.
    bool attachSurface(ANativeWindow *window);

    // Idempotent. Blocks until the worker has destroyed every Rive and EGL
    // object and exited (except when called from the status listener).
    void dispose();

    bool isTextureRing() const;

    // Texture-ring mode, on the consumer thread with a context of the shared
    // group current (the XR render thread). Takes the newest published frame
    // and makes the current context's GPU wait for Rive to finish it
    // (glWaitSync; the CPU does not block). False before the first frame, in
    // window mode, or while the ring is being rebuilt. Every true return must
    // be paired with endFrameRead() after the last GL command that samples
    // frame->texture, on the same thread, before returning to the caller's
    // frame loop.
    bool beginFrameRead(VRORivePanelFrame *frame);
    // Fences the sampling and releases the frame so the worker may reuse it.
    void endFrameRead();

    struct Impl;

private:
    explicit VRORivePanel(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> _impl;
};
