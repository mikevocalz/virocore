// Host test for VRORivePanel's worker thread and dispose(). Builds the real
// VRORivePanel.cpp (VRO_HAS_RIVE_RUNTIME=1) and VRORivePanelGeometry.cpp
// against link-time fakes of VRORivePanelEGL, VRORivePanelScene and
// VRORivePanelRing defined below, so the command queue, the worker loop and
// the thread lifecycle run unchanged without EGL, GL or rive. Run by
// scripts/test-rive-panel-worker.sh.

#include "VRORivePanel.h"
#include "VRORivePanelEGL.h"
#include "VRORivePanelRing.h"
#include "VRORivePanelScene.h"

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---- Platform stand-ins ------------------------------------------------------

int __android_log_print(int, const char *, const char *, ...) { return 0; }
EGLBoolean eglReleaseThread(void) { return 1; }

// ---- Fake EGL ------------------------------------------------------------------

VRORivePanelWindowRef::VRORivePanelWindowRef(ANativeWindow *window) : _window(window) {}
VRORivePanelWindowRef::~VRORivePanelWindowRef() = default;
VRORivePanelWindowRef::VRORivePanelWindowRef(VRORivePanelWindowRef &&other) noexcept
    : _window(other._window) {
    other._window = nullptr;
}
VRORivePanelWindowRef &VRORivePanelWindowRef::operator=(VRORivePanelWindowRef &&other) noexcept {
    _window = other._window;
    other._window = nullptr;
    return *this;
}
void VRORivePanelWindowRef::reset() { _window = nullptr; }

std::unique_ptr<VRORivePanelEGL> VRORivePanelEGL::create(std::string *) {
    return std::unique_ptr<VRORivePanelEGL>(new VRORivePanelEGL());
}
std::unique_ptr<VRORivePanelEGL> VRORivePanelEGL::createShared(EGLDisplay, EGLContext,
                                                               std::string *) {
    return std::unique_ptr<VRORivePanelEGL>(new VRORivePanelEGL());
}
VRORivePanelEGL::~VRORivePanelEGL() = default;
bool VRORivePanelEGL::attachWindow(VRORivePanelWindowRef window, int, int, std::string *) {
    _window = std::move(window);
    _windowSurface = reinterpret_cast<EGLSurface>(0x1);
    return true;
}
void VRORivePanelEGL::detachWindow() {
    _windowSurface = EGL_NO_SURFACE;
    _window.reset();
}
bool VRORivePanelEGL::resizeWindow(int, int, std::string *) { return true; }
VRORivePanelEGL::SwapResult VRORivePanelEGL::swapBuffers() { return SwapResult::Ok; }

// ---- Fake ring -------------------------------------------------------------------

bool VRORivePanelRing::create(int widthPx, int heightPx, std::string *) {
    std::lock_guard<std::mutex> lock(_mutex);
    _alive = true;
    _latest = -1;
    _reading = -1;
    _width = widthPx;
    _height = heightPx;
    return true;
}
void VRORivePanelRing::destroy() {
    std::lock_guard<std::mutex> lock(_mutex);
    _alive = false;
}
bool VRORivePanelRing::ready() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _alive;
}
int VRORivePanelRing::beginWrite() { return 0; }
GLuint VRORivePanelRing::texture(int) const { return 1; }
uint64_t VRORivePanelRing::endWrite(int) {
    std::lock_guard<std::mutex> lock(_mutex);
    return ++_sequence;
}
bool VRORivePanelRing::beginRead(VRORivePanelFrame *) { return false; }
void VRORivePanelRing::endRead() {}

// ---- Fake scene ------------------------------------------------------------------

namespace {
// Scene state the tests steer and observe. Touched by the worker only while a
// test waits for it, so the atomics are enough.
std::atomic<bool> gHasArtboard{true};
std::mutex gPointerMutex;
std::vector<VRORivePointerAction> gPointerActions;  // guarded by gPointerMutex
} // namespace

struct VRORivePanelScene::State {};

std::unique_ptr<VRORivePanelScene> VRORivePanelScene::create(std::string *) {
    return std::unique_ptr<VRORivePanelScene>(
        new VRORivePanelScene(std::unique_ptr<State>(new State())));
}
VRORivePanelScene::VRORivePanelScene(std::unique_ptr<State> state) : _state(std::move(state)) {}
VRORivePanelScene::~VRORivePanelScene() = default;
bool VRORivePanelScene::load(const uint8_t *, size_t, const std::string &, const std::string &,
                             std::string *) {
    return true;
}
bool VRORivePanelScene::selectArtboard(const std::string &, const std::string &, std::string *) {
    return true;
}
bool VRORivePanelScene::selectStateMachine(const std::string &, std::string *) { return true; }
bool VRORivePanelScene::hasFile() const { return true; }
bool VRORivePanelScene::hasArtboard() const { return gHasArtboard.load(); }
VRORiveRect VRORivePanelScene::artboardBounds() const { return VRORiveRect{0, 0, 64, 64}; }
bool VRORivePanelScene::setNumber(const std::string &, float, std::string *) { return true; }
bool VRORivePanelScene::setBoolean(const std::string &, bool, std::string *) { return true; }
bool VRORivePanelScene::setString(const std::string &, const std::string &, std::string *) {
    return true;
}
bool VRORivePanelScene::fireTrigger(const std::string &, std::string *) { return true; }
void VRORivePanelScene::pointer(VRORivePointerAction action, float, float, int) {
    std::lock_guard<std::mutex> lock(gPointerMutex);
    gPointerActions.push_back(action);
}
bool VRORivePanelScene::advance(float) { return false; }
bool VRORivePanelScene::render(int, int, const VRORiveAffine &, float, uint32_t, uint32_t,
                               std::string *) {
    return true;
}
void VRORivePanelScene::releaseRenderTarget() {}

// ---- Tests ---------------------------------------------------------------------

namespace {

int gFailures = 0;
int gChecks = 0;

void expect(bool condition, const char *what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

EGLDisplay fakeDisplay() { return reinterpret_cast<EGLDisplay>(0x10); }
EGLContext fakeContext() { return reinterpret_cast<EGLContext>(0x20); }

bool waitFor(const std::function<bool()> &condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// Runs body on its own thread. A deadlocked body cannot be joined or
// cancelled, so a timeout ends the process.
void runWithWatchdog(const char *name, const std::function<void()> &body) {
    std::packaged_task<void()> task(body);
    std::future<void> done = task.get_future();
    std::thread(std::move(task)).detach();
    if (done.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        std::fprintf(stderr, "FAIL: %s did not finish within 10 s (deadlock)\n", name);
        std::fflush(stderr);
        std::_Exit(1);
    }
}

// Another thread is joining the worker in dispose() while the status
// listener calls dispose() on the worker.
void testDisposeFromListenerWhileJoined() {
    runWithWatchdog("dispose from the listener during a join", [] {
        std::atomic<VRORivePanel *> self{nullptr};
        std::atomic<bool> listenerDisposed{false};
        VRORivePanel::StatusListener listener = [&](VRORivePanelState state,
                                                    const std::string &) {
            if (state != VRORivePanelState::Disposed) return;
            // Give the joining thread time to take the thread mutex first.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            self.load()->dispose();
            listenerDisposed.store(true);
        };
        std::shared_ptr<VRORivePanel> panel = VRORivePanel::createTextureRing(
            fakeDisplay(), fakeContext(), 64, 64, VRORivePanelOptions(), listener);
        expect(panel != nullptr, "texture-ring panel created");
        self.store(panel.get());
        expect(waitFor([&] { return panel->getState() == VRORivePanelState::Active; }),
               "panel becomes active");
        panel->dispose();
        expect(listenerDisposed.load(), "dispose() returned after the listener's dispose()");
        expect(panel->getState() == VRORivePanelState::Disposed, "state is Disposed");
    });
}

// dispose() from the listener is the only dispose; then the caller drops
// the panel. The worker must detach itself.
void testDisposeFromListenerAlone() {
    runWithWatchdog("dispose from the listener alone", [] {
        std::atomic<VRORivePanel *> self{nullptr};
        std::atomic<int> actives{0};
        std::atomic<bool> disposed{false};
        // Acts on the second Active (after setActive(false), setActive(true)),
        // by which time `self` is set.
        VRORivePanel::StatusListener listener = [&](VRORivePanelState state,
                                                    const std::string &) {
            if (state == VRORivePanelState::Active && ++actives == 2) self.load()->dispose();
            if (state == VRORivePanelState::Disposed) disposed.store(true);
        };
        std::shared_ptr<VRORivePanel> panel = VRORivePanel::createTextureRing(
            fakeDisplay(), fakeContext(), 64, 64, VRORivePanelOptions(), listener);
        self.store(panel.get());
        expect(waitFor([&] { return actives.load() == 1; }), "panel becomes active");
        // Two commands in one batch would leave the state unchanged and
        // publish nothing, so wait for Suspended in between.
        panel->setActive(false);
        expect(waitFor([&] { return panel->getState() == VRORivePanelState::Suspended; }),
               "panel suspends");
        panel->setActive(true);
        expect(waitFor([&] { return disposed.load(); }), "worker reaches Disposed");
        panel.reset();
    });
}

// The last reference is dropped inside the listener, so ~VRORivePanel runs
// on the worker.
void testLastReferenceDroppedInListener() {
    runWithWatchdog("last reference dropped in the listener", [] {
        std::shared_ptr<VRORivePanel> holder;
        std::mutex holderMutex;
        std::atomic<int> actives{0};
        std::atomic<bool> disposed{false};
        // Drops the last reference on the second Active, as above.
        VRORivePanel::StatusListener listener = [&](VRORivePanelState state,
                                                    const std::string &) {
            if (state == VRORivePanelState::Active && ++actives == 2) {
                std::shared_ptr<VRORivePanel> last;
                {
                    std::lock_guard<std::mutex> lock(holderMutex);
                    last.swap(holder);
                }
                last.reset();  // ~VRORivePanel on the worker
            }
            if (state == VRORivePanelState::Disposed) disposed.store(true);
        };
        VRORivePanel *raw = nullptr;
        {
            std::shared_ptr<VRORivePanel> panel = VRORivePanel::createTextureRing(
                fakeDisplay(), fakeContext(), 64, 64, VRORivePanelOptions(), listener);
            raw = panel.get();
            std::lock_guard<std::mutex> lock(holderMutex);
            holder = std::move(panel);
        }
        expect(waitFor([&] { return actives.load() == 1; }), "panel becomes active");
        // `holder` is now the only reference; `raw` stays valid until the
        // second Active reaches the listener.
        raw->setActive(false);
        expect(waitFor([&] { return raw->getState() == VRORivePanelState::Suspended; }),
               "panel suspends");
        raw->setActive(true);
        expect(waitFor([&] { return disposed.load(); }), "worker reaches Disposed");
        // Let the detached worker finish and free Impl before the next test.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
}

} // namespace

int main() {
    testDisposeFromListenerWhileJoined();
    testDisposeFromListenerAlone();
    testLastReferenceDroppedInListener();
    if (gFailures != 0) {
        std::fprintf(stderr, "rive panel worker: %d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("rive panel worker: %d checks passed\n", gChecks);
    return 0;
}
