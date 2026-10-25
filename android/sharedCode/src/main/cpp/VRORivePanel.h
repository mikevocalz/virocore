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
//  Threading: every method may be called from any thread except the status
//  listener's own thread (see setStatusListener). Calls enqueue a command for
//  the panel's worker thread, which owns all Rive objects, its own EGL context
//  and the EGL window surface. Nothing is shared with Viro's GL context; the
//  Surface's buffer queue is the hand-off.
//
//  Output: premultiplied alpha, sRGB-encoded bytes in an RGBA8 (non-sRGB)
//  window surface, cleared to transparent, corners clipped anti-aliased to
//  cornerRadiusPx. This is the scene-panel contract in ADR 0001 D6.
//
//  Availability: arm64-v8a builds with VIRO_WITH_RIVE. Elsewhere isAvailable()
//  is false and create() returns null.
//

#pragma once

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
    // Corner radius in panel pixels, computed by the caller from the panel's
    // radius in meters (radius * widthPixels / widthMeters). Clamped to half
    // the shorter side. Pixels outside the rounded rect are transparent and
    // pointer hits there do not reach Rive.
    float cornerRadiusPx = 0.0f;
    VRORivePanelFit fit = VRORivePanelFit::Contain;
    // rive::Alignment: -1 left/top, 0 centre, 1 right/bottom.
    float alignmentX = 0.0f;
    float alignmentY = 0.0f;
    // Premultiplied 0xAARRGGBB written before Rive draws. Transparent by
    // default so the panel's corners and empty areas show the scene.
    uint32_t clearColor = 0x00000000u;
    // Upper bound on frames per second while the state machine is animating.
    // Settled content produces no frames at all. Match the display rate.
    float maxFrameRateHz = 72.0f;
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
    // in a transparent corner is dropped; a press that started inside keeps
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

    struct Impl;

private:
    explicit VRORivePanel(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> _impl;
};
