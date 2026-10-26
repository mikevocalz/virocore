//
//  VRORivePanel.cpp
//  ViroRenderer
//
//  Public facade, command queue and worker thread for VRORivePanel.
//
//  Ownership and threads
//  - Caller threads only validate arguments and push commands onto a
//    mutex-protected queue. The mutex guards the queue and nothing else; no
//    Rive or EGL object is touched under it.
//  - The worker thread owns VRORivePanelEGL (EGL context, pbuffer, window
//    surface, window reference) and VRORivePanelScene (rive render context,
//    File, artboard, state machine, view model). It creates, uses and
//    destroys all of them.
//  - The worker sleeps on a condition variable: untimed while there is
//    nothing to draw, until the next frame deadline while the state machine
//    is animating. A settled panel uses no CPU or GPU.
//  - Texture-ring mode: the worker also owns VRORivePanelRing's textures and
//    fences (created and deleted on its shared context). The XR render
//    thread reads frames through the ring's own mutex, never through the
//    command queue, so a read never waits for the worker to wake.
//
//  Exceptions: this file is built with exceptions; rive's TU is not. Every
//  command and frame runs inside a try block on the worker; a throw tears
//  the GPU objects down and reports Lost. Public methods catch everything
//  (allocation, thread creation) and return false or null.
//

#include "VRORivePanel.h"

#include <android/log.h>

#define RIVE_PANEL_LOG_TAG "ViroRivePanel"

const char *VRORivePanelStateName(VRORivePanelState state) {
    switch (state) {
        case VRORivePanelState::Initializing: return "initializing";
        case VRORivePanelState::WaitingForSurface: return "waitingForSurface";
        case VRORivePanelState::Active: return "active";
        case VRORivePanelState::Suspended: return "suspended";
        case VRORivePanelState::Lost: return "lost";
        case VRORivePanelState::Disposed: return "disposed";
    }
    return "unknown";
}

#if defined(VRO_HAS_RIVE_RUNTIME) && VRO_HAS_RIVE_RUNTIME

#include "VRORivePanelEGL.h"
#include "VRORivePanelGeometry.h"
#include "VRORivePanelRing.h"
#include "VRORivePanelScene.h"

#include <EGL/egl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMaxDimension = 8192;
constexpr size_t kMaxFileBytes = size_t(64) << 20;
constexpr size_t kMaxNameLength = 1024;
constexpr size_t kMaxStringValueLength = size_t(64) << 10;
// Bounds the queue if the worker stalls. Lifecycle commands bypass the bound.
constexpr size_t kMaxQueuedCommands = 4096;
// A frame after a hitch advances at most this far, so animations do not jump.
constexpr float kMaxFrameSeconds = 0.1f;

struct CmdLoad { std::vector<uint8_t> bytes; };
struct CmdSelectArtboard { std::string name; };
struct CmdSelectStateMachine { std::string name; };
struct CmdNumber { std::string path; float value; };
struct CmdBoolean { std::string path; bool value; };
struct CmdString { std::string path; std::string value; };
struct CmdTrigger { std::string path; };
struct CmdPointer { VRORivePointerPhase phase; float x; float y; int id; };
struct CmdActive { bool active; };
struct CmdResize { int width; int height; };
struct CmdAttach { VRORivePanelWindowRef window; };
struct CmdSurfaceLost { std::shared_ptr<std::promise<void>> done; };

using Command = std::variant<CmdLoad, CmdSelectArtboard, CmdSelectStateMachine, CmdNumber,
                             CmdBoolean, CmdString, CmdTrigger, CmdPointer, CmdActive, CmdResize,
                             CmdAttach, CmdSurfaceLost>;

using ReplayValue = std::variant<float, bool, std::string>;

bool validName(const std::string &name) { return name.size() <= kMaxNameLength; }
bool validPath(const std::string &path) { return !path.empty() && path.size() <= kMaxNameLength; }
bool validDimension(int value) { return value >= 1 && value <= kMaxDimension; }

VRORivePanelOptions sanitize(VRORivePanelOptions options) {
    auto clampRadius = [](float v) { return std::isfinite(v) && v > 0.0f ? v : 0.0f; };
    options.cornerRadiusPx = clampRadius(options.cornerRadiusPx);
    options.coverageRadiusXPx = clampRadius(options.coverageRadiusXPx);
    options.coverageRadiusYPx = clampRadius(options.coverageRadiusYPx);
    auto clampAlignment = [](float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; };
    options.alignmentX = clampAlignment(options.alignmentX);
    options.alignmentY = clampAlignment(options.alignmentY);
    if (!std::isfinite(options.maxFrameRateHz) || options.maxFrameRateHz <= 0.0f) {
        options.maxFrameRateHz = 72.0f;
    }
    options.maxFrameRateHz = std::clamp(options.maxFrameRateHz, 1.0f, 240.0f);
    const int fit = static_cast<int>(options.fit);
    if (fit < static_cast<int>(VRORivePanelFit::Fill) ||
        fit > static_cast<int>(VRORivePanelFit::ScaleDown)) {
        options.fit = VRORivePanelFit::Contain;
    }
    return options;
}

} // namespace

// Hidden: a nested class takes VRORivePanel's default visibility otherwise,
// which would export every worker method.
struct __attribute__((visibility("hidden"))) VRORivePanel::Impl {
    Impl(const VRORivePanelOptions &opts, int w, int h, StatusListener statusListener,
         bool textureRing = false, EGLDisplay display = EGL_NO_DISPLAY,
         EGLContext share = EGL_NO_CONTEXT, FrameListener onFrame = nullptr)
        : options(opts), listener(std::move(statusListener)), ringMode(textureRing),
          shareDisplay(display), shareContext(share), frameListener(std::move(onFrame)),
          ring(textureRing ? std::make_shared<VRORivePanelRing>() : nullptr), width(w), height(h) {
        frameInterval = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / double(options.maxFrameRateHz)));
    }

    // ---- Set at creation, read-only afterwards ----
    const VRORivePanelOptions options;
    const StatusListener listener;
    const bool ringMode;
    const EGLDisplay shareDisplay;
    const EGLContext shareContext;
    const FrameListener frameListener;
    // Texture-ring mode only. The object is shared with the XR thread for
    // reads; its GL objects are created and deleted by the worker.
    const std::shared_ptr<VRORivePanelRing> ring;
    Clock::duration frameInterval{};

    // ---- Shared between callers and the worker ----
    std::mutex queueMutex;
    std::condition_variable wake;
    std::deque<Command> queue;       // guarded by queueMutex
    bool disposeRequested = false;   // guarded by queueMutex
    std::atomic<VRORivePanelState> state{VRORivePanelState::Initializing};
    std::atomic<std::thread::id> workerId{};
    std::mutex threadMutex;          // guards `thread` join/detach
    std::thread thread;
    // dispose() ran on the worker (status listener, or the last reference
    // dropped there): run() detaches the thread on its way out.
    std::atomic<bool> selfDisposing{false};
    // Set under threadMutex by a dispose() that is about to join.
    std::atomic<bool> joining{false};

    // ---- Worker thread only ----
    std::unique_ptr<VRORivePanelEGL> egl;
    std::unique_ptr<VRORivePanelScene> scene;
    VRORivePointerRouter router;
    int width;
    int height;
    bool active = true;
    bool gpuFailed = false;      // EGL/Rive objects torn down after an error
    bool surfaceFailed = false;  // window surface dropped after a swap error
    bool needsFrame = false;
    bool idle = true;            // last frame settled: next advance uses dt 0
    Clock::time_point lastFrame{};
    Clock::time_point nextFrame{};
    std::vector<uint8_t> fileBytes;  // last accepted file, for recovery
    std::string artboardName;
    std::string stateMachineName;
    std::map<std::string, ReplayValue> replay;  // view-model values to restore
    VRORivePanelState published = VRORivePanelState::Initializing;

    // ---- Caller side ----
    bool enqueue(Command command, bool bounded) {
        // Notify while holding the lock. Once the command is visible the
        // worker may apply it, publish a state, and drop the last reference
        // from the listener, destroying this Impl. Unlocking queueMutex must
        // be the caller's last touch of Impl, so `wake` cannot be signalled
        // after the unlock.
        std::lock_guard<std::mutex> lock(queueMutex);
        if (disposeRequested) return false;
        if (bounded && queue.size() >= kMaxQueuedCommands) return false;
        queue.push_back(std::move(command));
        wake.notify_one();
        return true;
    }

    bool onWorkerThread() const { return std::this_thread::get_id() == workerId.load(); }

    // ---- Worker side ----
    void run();
    bool initializeGpu();
    void tearDownGpu();
    bool canRender() const {
        if (ringMode) {
            return scene != nullptr && egl != nullptr && active && ring->ready();
        }
        return scene != nullptr && egl != nullptr && egl->hasWindow() && active && !surfaceFailed;
    }
    VRORivePanelState computeState() const {
        if (gpuFailed || surfaceFailed) return VRORivePanelState::Lost;
        if (ringMode) {
            if (egl == nullptr || !ring->ready()) return VRORivePanelState::WaitingForSurface;
        } else if (egl == nullptr || !egl->hasWindow()) {
            return VRORivePanelState::WaitingForSurface;
        }
        return active ? VRORivePanelState::Active : VRORivePanelState::Suspended;
    }
    bool createRing();
    void detachSelf();
    void notify(VRORivePanelState s, const std::string &detail);
    void publishIfChanged();
    void report(const std::string &detail);
    void failGpu(const std::string &detail);
    void dropWindow();
    VRORiveAffine artboardToPanel() const;
    void restoreContent();

    void apply(Command &command);
    void apply(CmdLoad &cmd);
    void apply(CmdSelectArtboard &cmd);
    void apply(CmdSelectStateMachine &cmd);
    void apply(CmdNumber &cmd);
    void apply(CmdBoolean &cmd);
    void apply(CmdString &cmd);
    void apply(CmdTrigger &cmd);
    void apply(CmdPointer &cmd);
    void apply(CmdActive &cmd);
    void apply(CmdResize &cmd);
    void apply(CmdAttach &cmd);
    void apply(CmdSurfaceLost &cmd);
    void renderFrame();
};

void VRORivePanel::Impl::notify(VRORivePanelState s, const std::string &detail) {
    state.store(s);
    published = s;
    if (!detail.empty()) {
        __android_log_print(ANDROID_LOG_WARN, RIVE_PANEL_LOG_TAG, "[%s] %s",
                            VRORivePanelStateName(s), detail.c_str());
    }
    if (!listener) return;
    try {
        listener(s, detail);
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG,
                            "status listener threw; ignored");
    }
}

void VRORivePanel::Impl::publishIfChanged() {
    const VRORivePanelState s = computeState();
    if (s != published) notify(s, std::string());
}

void VRORivePanel::Impl::report(const std::string &detail) { notify(computeState(), detail); }

void VRORivePanel::Impl::tearDownGpu() {
    // Scene first: its rive objects hold GL resources of egl's context, which
    // is still current here (including the FBO attached to a ring texture).
    // Then the ring, which waits for any XR-thread read and its fence. Then
    // the EGL surface, then the window ref, then the context.
    scene.reset();
    if (ring != nullptr && egl != nullptr) ring->destroy();
    egl.reset();
    router.reset();
}

bool VRORivePanel::Impl::createRing() {
    std::string error;
    if (!ring->create(width, height, &error)) {
        failGpu("ring: " + error);
        return false;
    }
    needsFrame = true;
    idle = true;
    return true;
}

// Worker, after teardown, when dispose() ran on this thread. Detaches unless
// another thread is joining. Never blocks on threadMutex, which a joiner holds
// for the whole join; create() and a joiner that has not set `joining` yet
// hold it only briefly.
void VRORivePanel::Impl::detachSelf() {
    while (!joining.load()) {
        std::unique_lock<std::mutex> lock(threadMutex, std::try_to_lock);
        if (lock.owns_lock()) {
            if (!joining.load() && thread.joinable()) thread.detach();
            return;
        }
        std::this_thread::yield();
    }
}

void VRORivePanel::Impl::failGpu(const std::string &detail) {
    tearDownGpu();
    gpuFailed = true;
    needsFrame = false;
    report(detail);
}

void VRORivePanel::Impl::dropWindow() {
    if (scene != nullptr) scene->releaseRenderTarget();
    if (egl != nullptr) egl->detachWindow();
    router.reset();
}

bool VRORivePanel::Impl::initializeGpu() {
    std::string error;
    egl = ringMode ? VRORivePanelEGL::createShared(shareDisplay, shareContext, &error)
                   : VRORivePanelEGL::create(&error);
    if (egl == nullptr) {
        failGpu(error);
        return false;
    }
    scene = VRORivePanelScene::create(&error);
    if (scene == nullptr) {
        failGpu(error);
        return false;
    }
    if (ringMode && !createRing()) return false;
    gpuFailed = false;
    restoreContent();
    return true;
}

// After a GPU rebuild: re-import the last accepted file with the same
// selection and re-apply view-model values written since it loaded.
void VRORivePanel::Impl::restoreContent() {
    if (fileBytes.empty() || scene == nullptr) return;
    std::string error;
    if (!scene->load(fileBytes.data(), fileBytes.size(), artboardName, stateMachineName, &error)) {
        report("restore: " + error);
        return;
    }
    for (const auto &entry : replay) {
        std::string ignored;
        if (const float *number = std::get_if<float>(&entry.second)) {
            scene->setNumber(entry.first, *number, &ignored);
        } else if (const bool *boolean = std::get_if<bool>(&entry.second)) {
            scene->setBoolean(entry.first, *boolean, &ignored);
        } else if (const std::string *text = std::get_if<std::string>(&entry.second)) {
            scene->setString(entry.first, *text, &ignored);
        }
    }
    needsFrame = true;
    idle = true;
}

VRORiveAffine VRORivePanel::Impl::artboardToPanel() const {
    return VRORiveComputeAlignment(options.fit, options.alignmentX, options.alignmentY,
                                   float(width), float(height), scene->artboardBounds());
}

void VRORivePanel::Impl::apply(CmdLoad &cmd) {
    if (scene == nullptr) {
        // GPU is down; imported when it comes back.
        fileBytes = std::move(cmd.bytes);
        replay.clear();
        return;
    }
    std::string error;
    if (!scene->load(cmd.bytes.data(), cmd.bytes.size(), artboardName, stateMachineName, &error)) {
        report("loadFile: " + error);
        return;
    }
    fileBytes = std::move(cmd.bytes);
    replay.clear();
    router.reset();
    needsFrame = true;
    idle = true;
    if (!error.empty()) report("loadFile: " + error);
}

void VRORivePanel::Impl::apply(CmdSelectArtboard &cmd) {
    const std::string previous = artboardName;
    artboardName = cmd.name;
    if (scene == nullptr) return;
    std::string error;
    if (!scene->selectArtboard(artboardName, stateMachineName, &error)) {
        artboardName = previous;
        report("selectArtboard: " + error);
        return;
    }
    replay.clear();
    router.reset();
    needsFrame = true;
    idle = true;
    if (!error.empty()) report("selectArtboard: " + error);
}

void VRORivePanel::Impl::apply(CmdSelectStateMachine &cmd) {
    const std::string previous = stateMachineName;
    stateMachineName = cmd.name;
    if (scene == nullptr) return;
    std::string error;
    if (!scene->selectStateMachine(stateMachineName, &error)) {
        stateMachineName = previous;
        report("selectStateMachine: " + error);
        return;
    }
    router.reset();
    needsFrame = true;
    idle = true;
}

void VRORivePanel::Impl::apply(CmdNumber &cmd) {
    replay[cmd.path] = cmd.value;
    if (scene == nullptr) return;
    std::string error;
    if (!scene->setNumber(cmd.path, cmd.value, &error)) {
        report("setNumber: " + error);
        return;
    }
    needsFrame = true;
}

void VRORivePanel::Impl::apply(CmdBoolean &cmd) {
    replay[cmd.path] = cmd.value;
    if (scene == nullptr) return;
    std::string error;
    if (!scene->setBoolean(cmd.path, cmd.value, &error)) {
        report("setBoolean: " + error);
        return;
    }
    needsFrame = true;
}

void VRORivePanel::Impl::apply(CmdString &cmd) {
    replay[cmd.path] = cmd.value;
    if (scene == nullptr) return;
    std::string error;
    if (!scene->setString(cmd.path, cmd.value, &error)) {
        report("setString: " + error);
        return;
    }
    needsFrame = true;
}

void VRORivePanel::Impl::apply(CmdTrigger &cmd) {
    // Triggers are events, not state: never replayed.
    if (scene == nullptr) return;
    std::string error;
    if (!scene->fireTrigger(cmd.path, &error)) {
        report("fireTrigger: " + error);
        return;
    }
    needsFrame = true;
}

void VRORivePanel::Impl::apply(CmdPointer &cmd) {
    // Routed even with no artboard to send it to, so an up still releases a
    // captured pointer and its next press starts fresh.
    // Hits follow the corner the consumer draws (coverage >= 0.5).
    float radiusX = 0.0f, radiusY = 0.0f;
    VRORivePanelHitRadii(options, &radiusX, &radiusY);
    const bool inside = radiusX == radiusY
                            ? VRORiveRoundedRectContains(cmd.x, cmd.y, float(width), float(height),
                                                         radiusX)
                            : VRORiveRoundedRectContains(cmd.x, cmd.y, float(width), float(height),
                                                         radiusX, radiusY);
    const VRORivePointerAction action = router.route(cmd.phase, inside, cmd.id);
    if (action == VRORivePointerAction::Drop) return;
    if (scene == nullptr || !scene->hasArtboard()) return;
    VRORiveAffine inverse;
    if (!VRORiveInvert(artboardToPanel(), &inverse)) return;
    float artboardX = 0.0f, artboardY = 0.0f;
    VRORiveMapPoint(inverse, cmd.x, cmd.y, &artboardX, &artboardY);
    scene->pointer(action, artboardX, artboardY, cmd.id);
    needsFrame = true;
}

void VRORivePanel::Impl::apply(CmdActive &cmd) {
    active = cmd.active;
    if (active && ringMode && gpuFailed) {
        // No window to re-attach in ring mode: activation is the recovery.
        tearDownGpu();
        if (!initializeGpu()) return;
    }
    if (active) {
        needsFrame = true;
        idle = true;
    }
}

void VRORivePanel::Impl::apply(CmdResize &cmd) {
    width = cmd.width;
    height = cmd.height;
    if (ringMode) {
        if (egl != nullptr && scene != nullptr) {
            scene->releaseRenderTarget();
            ring->destroy();
            createRing();
        }
        return;
    }
    if (egl != nullptr) {
        std::string error;
        if (!egl->resizeWindow(width, height, &error)) report("resize: " + error);
    }
    needsFrame = true;
}

void VRORivePanel::Impl::apply(CmdAttach &cmd) {
    if (egl == nullptr || scene == nullptr) {
        tearDownGpu();
        if (!initializeGpu()) return;  // cmd.window is released on return
    }
    dropWindow();
    surfaceFailed = false;
    std::string error;
    if (!egl->attachWindow(std::move(cmd.window), width, height, &error)) {
        surfaceFailed = true;
        report("attachSurface: " + error);
        return;
    }
    needsFrame = true;
    idle = true;
}

// The caller is released by run() once this returns (or throws).
void VRORivePanel::Impl::apply(CmdSurfaceLost &) {
    dropWindow();
    surfaceFailed = false;
}

void VRORivePanel::Impl::apply(Command &command) {
    std::visit([this](auto &cmd) { apply(cmd); }, command);
}

void VRORivePanel::Impl::renderFrame() {
    const Clock::time_point now = Clock::now();
    float dt = 0.0f;
    if (!idle) {
        dt = std::chrono::duration<float>(now - lastFrame).count();
        dt = std::clamp(dt, 0.0f, kMaxFrameSeconds);
    }
    const bool more = scene->advance(dt);
    std::string error;
    if (ringMode) {
        const int slot = ring->beginWrite();
        if (slot < 0) {
            // Unreachable with three slots while the ring is alive.
            needsFrame = false;
            report("ring: no free slot");
            return;
        }
        if (!scene->render(width, height, artboardToPanel(), options.cornerRadiusPx,
                           options.clearColor, ring->texture(slot), &error)) {
            failGpu("render: " + error);
            return;
        }
        const uint64_t sequence = ring->endWrite(slot);
        if (sequence != 0 && frameListener) {
            try {
                frameListener(sequence);
            } catch (...) {
                __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG,
                                    "frame listener threw; ignored");
            }
        }
        lastFrame = now;
        nextFrame = now + frameInterval;
        idle = !more;
        needsFrame = more;
        return;
    }
    if (!scene->render(width, height, artboardToPanel(), options.cornerRadiusPx,
                       options.clearColor, 0, &error)) {
        failGpu("render: " + error);
        return;
    }
    switch (egl->swapBuffers()) {
        case VRORivePanelEGL::SwapResult::Ok:
            break;
        case VRORivePanelEGL::SwapResult::SurfaceLost:
            // The consumer abandoned the Surface. Keep the context and the
            // content; attachSurface() brings the panel back.
            dropWindow();
            surfaceFailed = true;
            needsFrame = false;
            report(egl->lastError());
            return;
        case VRORivePanelEGL::SwapResult::ContextLost:
            failGpu(egl->lastError());
            return;
        case VRORivePanelEGL::SwapResult::Failed:
            // Logged by swapBuffers. The frame is lost; the next one retries.
            break;
    }
    lastFrame = now;
    nextFrame = now + frameInterval;
    idle = !more;
    needsFrame = more;
}

void VRORivePanel::Impl::run() {
    workerId.store(std::this_thread::get_id());
    try {
        initializeGpu();
    } catch (const std::exception &e) {
        failGpu(std::string("initialize: ") + e.what());
    } catch (...) {
        failGpu("initialize: unknown exception");
    }
    publishIfChanged();

    std::deque<Command> batch;
    while (true) {
        bool exiting = false;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            while (queue.empty() && !disposeRequested) {
                if (canRender() && needsFrame) {
                    if (Clock::now() >= nextFrame) break;
                    wake.wait_until(lock, nextFrame);
                } else {
                    wake.wait(lock);
                }
            }
            exiting = disposeRequested;
            batch.swap(queue);
        }
        // Commands queued before dispose() are not applied; teardown follows.
        if (exiting) break;

        // Everything queued since the last wake is applied before one
        // advance and one frame: view-model writes and pointer events are
        // batched per frame.
        while (!batch.empty()) {
            Command command = std::move(batch.front());
            batch.pop_front();
            try {
                apply(command);
            } catch (const std::exception &e) {
                failGpu(std::string("command: ") + e.what());
            } catch (...) {
                failGpu("command: unknown exception");
            }
            if (auto *lost = std::get_if<CmdSurfaceLost>(&command)) {
                lost->done->set_value();
            }
        }

        if (canRender() && needsFrame && Clock::now() >= nextFrame) {
            try {
                renderFrame();
            } catch (const std::exception &e) {
                failGpu(std::string("frame: ") + e.what());
            } catch (...) {
                failGpu("frame: unknown exception");
            }
        }
        publishIfChanged();
    }

    // Commands still queued are dropped, but callers blocked in surfaceLost()
    // are released only after the window surface is gone.
    std::deque<Command> leftover;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        leftover.swap(queue);
    }
    for (Command &command : batch) leftover.push_back(std::move(command));
    try {
        tearDownGpu();
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "teardown threw");
    }
    eglReleaseThread();
    for (Command &command : leftover) {
        if (auto *lost = std::get_if<CmdSurfaceLost>(&command)) {
            lost->done->set_value();
        }
    }
    if (selfDisposing.load()) detachSelf();
    // The listener can still call dispose() (or drop the last reference) on
    // this notification, so check once more after it returns.
    notify(VRORivePanelState::Disposed, std::string());
    if (selfDisposing.load()) detachSelf();
}

bool VRORivePanel::isAvailable() { return true; }

std::shared_ptr<VRORivePanel> VRORivePanel::create(ANativeWindow *window, int widthPx,
                                                   int heightPx, VRORivePanelOptions options,
                                                   StatusListener listener) {
    if (!validDimension(widthPx) || !validDimension(heightPx)) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG,
                            "create: size %dx%d outside 1..%d", widthPx, heightPx, kMaxDimension);
        return nullptr;
    }
    try {
        auto impl = std::make_shared<Impl>(sanitize(options), widthPx, heightPx, std::move(listener));
        if (window != nullptr) {
            impl->queue.push_back(CmdAttach{VRORivePanelWindowRef(window)});
        }
        {
            std::lock_guard<std::mutex> lock(impl->threadMutex);
            // The thread keeps Impl alive until it exits, so dispose() from
            // the listener can detach instead of joining itself.
            impl->thread = std::thread([impl] { impl->run(); });
            impl->workerId.store(impl->thread.get_id());
        }
        return std::shared_ptr<VRORivePanel>(new VRORivePanel(std::move(impl)));
    } catch (const std::exception &e) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "create: %s", e.what());
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "create: unknown exception");
    }
    return nullptr;
}

std::shared_ptr<VRORivePanel> VRORivePanel::createTextureRing(EGLDisplay display,
                                                              EGLContext shareContext,
                                                              int widthPx, int heightPx,
                                                              VRORivePanelOptions options,
                                                              StatusListener listener,
                                                              FrameListener frameListener) {
    if (!validDimension(widthPx) || !validDimension(heightPx)) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG,
                            "createTextureRing: size %dx%d outside 1..%d", widthPx, heightPx,
                            kMaxDimension);
        return nullptr;
    }
    if (display == EGL_NO_DISPLAY || shareContext == EGL_NO_CONTEXT) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG,
                            "createTextureRing: no EGL display or share context");
        return nullptr;
    }
    try {
        auto impl = std::make_shared<Impl>(sanitize(options), widthPx, heightPx,
                                           std::move(listener), true, display, shareContext,
                                           std::move(frameListener));
        {
            std::lock_guard<std::mutex> lock(impl->threadMutex);
            impl->thread = std::thread([impl] { impl->run(); });
            impl->workerId.store(impl->thread.get_id());
        }
        return std::shared_ptr<VRORivePanel>(new VRORivePanel(std::move(impl)));
    } catch (const std::exception &e) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG, "createTextureRing: %s",
                            e.what());
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, RIVE_PANEL_LOG_TAG,
                            "createTextureRing: unknown exception");
    }
    return nullptr;
}

VRORivePanel::VRORivePanel(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}

VRORivePanel::~VRORivePanel() { dispose(); }

bool VRORivePanel::isTextureRing() const { return _impl->ringMode; }

bool VRORivePanel::beginFrameRead(VRORivePanelFrame *frame) {
    if (!_impl->ringMode || frame == nullptr) return false;
    return _impl->ring->beginRead(frame);
}

void VRORivePanel::endFrameRead() {
    if (_impl->ringMode) _impl->ring->endRead();
}

VRORivePanelState VRORivePanel::getState() const { return _impl->state.load(); }

bool VRORivePanel::loadFile(const uint8_t *bytes, size_t length) {
    if (bytes == nullptr || length == 0 || length > kMaxFileBytes) return false;
    try {
        return _impl->enqueue(CmdLoad{std::vector<uint8_t>(bytes, bytes + length)}, false);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::selectArtboard(const std::string &name) {
    if (!validName(name)) return false;
    try {
        return _impl->enqueue(CmdSelectArtboard{name}, false);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::selectStateMachine(const std::string &name) {
    if (!validName(name)) return false;
    try {
        return _impl->enqueue(CmdSelectStateMachine{name}, false);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::setNumber(const std::string &path, float value) {
    if (!validPath(path) || !std::isfinite(value)) return false;
    try {
        return _impl->enqueue(CmdNumber{path, value}, true);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::setBoolean(const std::string &path, bool value) {
    if (!validPath(path)) return false;
    try {
        return _impl->enqueue(CmdBoolean{path, value}, true);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::setString(const std::string &path, const std::string &value) {
    if (!validPath(path) || value.size() > kMaxStringValueLength) return false;
    try {
        return _impl->enqueue(CmdString{path, value}, true);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::fireTrigger(const std::string &path) {
    if (!validPath(path)) return false;
    try {
        return _impl->enqueue(CmdTrigger{path}, true);
    } catch (...) {
        return false;
    }
}

namespace {
bool validPointer(float x, float y) { return std::isfinite(x) && std::isfinite(y); }
} // namespace

bool VRORivePanel::pointerDown(float xPx, float yPx, int pointerId) {
    if (!validPointer(xPx, yPx)) return false;
    try {
        return _impl->enqueue(CmdPointer{VRORivePointerPhase::Down, xPx, yPx, pointerId}, true);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::pointerMove(float xPx, float yPx, int pointerId) {
    if (!validPointer(xPx, yPx)) return false;
    try {
        return _impl->enqueue(CmdPointer{VRORivePointerPhase::Move, xPx, yPx, pointerId}, true);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::pointerUp(float xPx, float yPx, int pointerId) {
    if (!validPointer(xPx, yPx)) return false;
    try {
        // Unbounded: dropping an up would leave Rive holding the press.
        return _impl->enqueue(CmdPointer{VRORivePointerPhase::Up, xPx, yPx, pointerId}, false);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::setActive(bool active) {
    try {
        return _impl->enqueue(CmdActive{active}, false);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::resize(int widthPx, int heightPx) {
    if (!validDimension(widthPx) || !validDimension(heightPx)) return false;
    try {
        return _impl->enqueue(CmdResize{widthPx, heightPx}, false);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::attachSurface(ANativeWindow *window) {
    if (window == nullptr || _impl->ringMode) return false;
    try {
        return _impl->enqueue(CmdAttach{VRORivePanelWindowRef(window)}, false);
    } catch (...) {
        return false;
    }
}

bool VRORivePanel::surfaceLost() {
    if (_impl->ringMode) return false;
    try {
        auto done = std::make_shared<std::promise<void>>();
        std::future<void> finished = done->get_future();
        if (!_impl->enqueue(CmdSurfaceLost{done}, false)) return false;
        if (_impl->onWorkerThread()) {
            // Called from the status listener: the worker handles it right
            // after the listener returns. Waiting here would deadlock.
            return true;
        }
        finished.wait();
        return true;
    } catch (...) {
        return false;
    }
}

void VRORivePanel::dispose() {
    Impl &impl = *_impl;
    {
        std::lock_guard<std::mutex> lock(impl.queueMutex);
        impl.disposeRequested = true;
    }
    impl.wake.notify_all();
    if (impl.onWorkerThread()) {
        // From the listener (or the last reference dropped there): the
        // worker finishes teardown after the listener returns, and its lambda
        // holds Impl alive until then. threadMutex is not taken here: another
        // thread may hold it while joining this one. run() detaches instead,
        // unless that joiner is waiting for it.
        impl.selfDisposing.store(true);
        return;
    }
    std::lock_guard<std::mutex> lock(impl.threadMutex);
    if (!impl.thread.joinable()) return;
    impl.joining.store(true);
    impl.thread.join();
}

#else // Rive is not built for this ABI (armeabi-v7a, or VIRO_WITH_RIVE=OFF).

struct __attribute__((visibility("hidden"))) VRORivePanel::Impl {};

bool VRORivePanel::isAvailable() { return false; }

std::shared_ptr<VRORivePanel> VRORivePanel::create(ANativeWindow *, int, int, VRORivePanelOptions,
                                                   StatusListener) {
    __android_log_print(ANDROID_LOG_WARN, RIVE_PANEL_LOG_TAG,
                        "create: rive-runtime is not built for this ABI");
    return nullptr;
}

// No instance can exist (create returns null); these keep the exported API
// identical across ABIs so consumers link everywhere.
VRORivePanel::VRORivePanel(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
VRORivePanel::~VRORivePanel() = default;
VRORivePanelState VRORivePanel::getState() const { return VRORivePanelState::Disposed; }
bool VRORivePanel::loadFile(const uint8_t *, size_t) { return false; }
bool VRORivePanel::selectArtboard(const std::string &) { return false; }
bool VRORivePanel::selectStateMachine(const std::string &) { return false; }
bool VRORivePanel::setNumber(const std::string &, float) { return false; }
bool VRORivePanel::setBoolean(const std::string &, bool) { return false; }
bool VRORivePanel::setString(const std::string &, const std::string &) { return false; }
bool VRORivePanel::fireTrigger(const std::string &) { return false; }
bool VRORivePanel::pointerDown(float, float, int) { return false; }
bool VRORivePanel::pointerMove(float, float, int) { return false; }
bool VRORivePanel::pointerUp(float, float, int) { return false; }
bool VRORivePanel::setActive(bool) { return false; }
bool VRORivePanel::resize(int, int) { return false; }
bool VRORivePanel::surfaceLost() { return false; }
bool VRORivePanel::attachSurface(ANativeWindow *) { return false; }
void VRORivePanel::dispose() {}
std::shared_ptr<VRORivePanel> VRORivePanel::createTextureRing(EGLDisplay, EGLContext, int, int,
                                                              VRORivePanelOptions, StatusListener,
                                                              FrameListener) {
    __android_log_print(ANDROID_LOG_WARN, RIVE_PANEL_LOG_TAG,
                        "createTextureRing: rive-runtime is not built for this ABI");
    return nullptr;
}
bool VRORivePanel::isTextureRing() const { return false; }
bool VRORivePanel::beginFrameRead(VRORivePanelFrame *) { return false; }
void VRORivePanel::endFrameRead() {}

#endif
