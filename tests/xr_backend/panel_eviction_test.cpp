// Host test for VROOpenXRPanelCompositor eviction reporting. Builds the real
// compositor against the vendored OpenXR headers, minimal EGL/GLES/JNI type
// shims (tests/host_shims) and the fake runtime below, which
// grants every acquire, wait and release. Run by
// scripts/test-openxr-panel-eviction.sh.

#include "VROOpenXRPanelCompositor.h"

#include <android/log.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ---- Fake runtime -----------------------------------------------------------

namespace {
uintptr_t gNextHandle = 0x1000;
int gFinishCalls = 0;
} // namespace

void glFlush() {}
void glFinish() { ++gFinishCalls; }

// The compositor logs its live panel swapchain count on each create and
// destroy. Keep the last value so main() can check it returns to 0.
namespace {
int gLoggedLivePanelSwapchains = -1;
} // namespace

int __android_log_print(int, const char *, const char *fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    int live = 0;
    if (std::sscanf(line, "OpenXR panel swapchains: live=%d", &live) == 1) {
        gLoggedLivePanelSwapchains = live;
    }
    return 0;
}

extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession, const XrSwapchainCreateInfo *,
                                                 XrSwapchain *swapchain) {
    *swapchain = reinterpret_cast<XrSwapchain>(gNextHandle++);
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain, uint32_t capacity,
                                                          uint32_t *count,
                                                          XrSwapchainImageBaseHeader *images) {
    *count = 3;
    if (capacity >= 3 && images != nullptr) {
        auto *gles = reinterpret_cast<XrSwapchainImageOpenGLESKHR *>(images);
        for (uint32_t i = 0; i < 3; ++i) gles[i].image = 100 + i;
    }
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain,
                                                       const XrSwapchainImageAcquireInfo *,
                                                       uint32_t *index) {
    *index = 0;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo *) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain,
                                                       const XrSwapchainImageReleaseInfo *) {
    return XR_SUCCESS;
}
} // extern "C"

// ---- Test ------------------------------------------------------------------

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

struct Event {
    std::string id;
    VROOpenXRPanelPresentation presentation;
    VROOpenXRPanelEvictionReason reason;
};

VROOpenXRPanelDesc quad(const std::string &id) {
    VROOpenXRPanelDesc desc;
    desc.id = id;
    return desc;
}

XrSession fakeSession() { return reinterpret_cast<XrSession>(uintptr_t(0x42)); }
XrSpace fakeSpace() { return reinterpret_cast<XrSpace>(uintptr_t(0x43)); }

size_t frame(VROOpenXRPanelCompositor &compositor, uint32_t slots,
             VROOpenXRPanelEvictionReason reason = VROOpenXRPanelEvictionReason::LayerBudget) {
    std::vector<const XrCompositionLayerBaseHeader *> layers;
    compositor.appendLayers(fakeSpace(), slots, 13888888, layers, reason);
    return layers.size();
}

void testBudgetEviction() {
    VROOpenXRPanelCompositor compositor(fakeSession(), GL_SRGB8_ALPHA8, 8, false);
    std::vector<Event> events;
    compositor.setPresentationListener(
        [&events](const std::string &id, VROOpenXRPanelPresentation p,
                  VROOpenXRPanelEvictionReason r) { events.push_back({id, p, r}); });
    auto ok = [](GLuint, uint32_t, uint32_t) { return true; };
    expect(compositor.addPanel(quad("a"), ok) == VROOpenXRPanelError::None, "add a");
    expect(compositor.addPanel(quad("b"), ok) == VROOpenXRPanelError::None, "add b");
    expect(compositor.addPanel(quad("c"), ok) == VROOpenXRPanelError::None, "add c");

    // Two free slots for three panels: the third, in registration order, is
    // evicted and reported, not dropped silently.
    expect(frame(compositor, 2) == 2, "two layers submitted");
    expect(events.size() == 3, "three presentation events");
    expect(events.size() == 3 && events[0].id == "a" &&
               events[0].presentation == VROOpenXRPanelPresentation::Compositor &&
               events[0].reason == VROOpenXRPanelEvictionReason::None,
           "a composited");
    expect(events.size() == 3 && events[1].id == "b" &&
               events[1].presentation == VROOpenXRPanelPresentation::Compositor,
           "b composited");
    expect(events.size() == 3 && events[2].id == "c" &&
               events[2].presentation == VROOpenXRPanelPresentation::Evicted &&
               events[2].reason == VROOpenXRPanelEvictionReason::LayerBudget,
           "c evicted for the layer budget");
    expect(compositor.presentation("c") == VROOpenXRPanelPresentation::Evicted, "c reads Evicted");

    // Same budget again: no repeated events.
    events.clear();
    expect(frame(compositor, 2) == 2, "steady state submits two layers");
    expect(events.empty(), "no event when nothing changes");

    // A slot frees: c comes back by itself.
    events.clear();
    expect(frame(compositor, 3) == 3, "three layers once room exists");
    expect(events.size() == 1 && events[0].id == "c" &&
               events[0].presentation == VROOpenXRPanelPresentation::Compositor,
           "c returns to the compositor");

    // Runtime cap after XR_ERROR_LAYER_LIMIT_EXCEEDED: reason is passed through.
    events.clear();
    expect(frame(compositor, 1, VROOpenXRPanelEvictionReason::LayerLimitExceeded) == 1,
           "one layer under the runtime cap");
    expect(events.size() == 2, "b and c evicted under the cap");
    bool reasons = events.size() == 2;
    for (const Event &e : events) {
        reasons = reasons && e.presentation == VROOpenXRPanelPresentation::Evicted &&
                  e.reason == VROOpenXRPanelEvictionReason::LayerLimitExceeded;
    }
    expect(reasons, "eviction reason is XR_ERROR_LAYER_LIMIT_EXCEEDED");

    // No slots at all: previously an early return, now every visible panel
    // is reported.
    events.clear();
    expect(frame(compositor, 0) == 0, "no layers with no slots");
    expect(events.size() == 1 && events[0].id == "a" &&
               events[0].presentation == VROOpenXRPanelPresentation::Evicted,
           "a evicted with zero slots");
}

void testHiddenAndPendingPanels() {
    VROOpenXRPanelCompositor compositor(fakeSession(), GL_RGBA8, 8, false);
    std::vector<Event> events;
    compositor.setPresentationListener(
        [&events](const std::string &id, VROOpenXRPanelPresentation p,
                  VROOpenXRPanelEvictionReason r) { events.push_back({id, p, r}); });
    bool firstReady = false;
    auto notReady = [&firstReady](GLuint, uint32_t, uint32_t) { return firstReady; };
    auto ok = [](GLuint, uint32_t, uint32_t) { return true; };
    compositor.addPanel(quad("slow"), notReady);
    compositor.addPanel(quad("hidden"), ok);
    compositor.addPanel(quad("fast"), ok);
    compositor.setPanelVisible("hidden", false);

    // "slow" has no first frame, so it takes no slot and is not reported;
    // the one slot goes to "fast". The hidden panel is never reported.
    expect(frame(compositor, 1) == 1, "one layer");
    expect(events.size() == 1 && events[0].id == "fast" &&
               events[0].presentation == VROOpenXRPanelPresentation::Compositor,
           "only fast is reported");
    expect(compositor.presentation("slow") == VROOpenXRPanelPresentation::Pending,
           "slow stays pending");

    // Once slow draws, it takes the slot ahead of fast.
    events.clear();
    firstReady = true;
    expect(frame(compositor, 1) == 1, "still one layer");
    expect(events.size() == 2, "slow in, fast out");
    expect(compositor.presentation("slow") == VROOpenXRPanelPresentation::Compositor, "slow shown");
    expect(compositor.presentation("fast") == VROOpenXRPanelPresentation::Evicted, "fast evicted");
    expect(compositor.presentation("hidden") == VROOpenXRPanelPresentation::Pending,
           "hidden never reported");

    // Removing a panel destroys its swapchain after draining GPU work.
    const int finishes = gFinishCalls;
    expect(compositor.removePanel("slow"), "remove slow");
    expect(gFinishCalls == finishes + 1, "glFinish before xrDestroySwapchain");
    events.clear();
    expect(frame(compositor, 1) == 1, "fast gets the freed slot");
    expect(events.size() == 1 && events[0].id == "fast" &&
               events[0].presentation == VROOpenXRPanelPresentation::Compositor,
           "fast back on the compositor");
}

// validatePanel answers what addPanel would, without a producer and without
// allocating a swapchain, so a caller can refuse a bad desc before building
// one (addRiveCompositorPanel starts a Rive worker and a texture ring).
void testValidatePanel() {
    VROOpenXRPanelCompositor compositor(fakeSession(), GL_RGBA8, 2, false);
    auto ok = [](GLuint, uint32_t, uint32_t) { return true; };
    const uintptr_t handles = gNextHandle;
    expect(compositor.validatePanel(quad("v")) == VROOpenXRPanelError::None, "valid desc");
    expect(gNextHandle == handles, "validation allocates no swapchain");

    VROOpenXRPanelDesc desc = quad("v");
    desc.widthPixels = 15;
    expect(compositor.validatePanel(desc) == VROOpenXRPanelError::InvalidPixelSize, "15 px wide");
    desc = quad("v");
    desc.heightPixels = 4097;
    expect(compositor.validatePanel(desc) == VROOpenXRPanelError::InvalidPixelSize, "4097 px high");
    desc = quad("v");
    desc.widthPixels = 16;
    desc.heightPixels = 4096;
    expect(compositor.validatePanel(desc) == VROOpenXRPanelError::None, "16 x 4096 px");
    desc = quad("v");
    desc.widthMeters = std::nanf("");
    expect(compositor.validatePanel(desc) == VROOpenXRPanelError::InvalidDimensions, "NaN width");
    desc = quad("v");
    desc.heightMeters = INFINITY;
    expect(compositor.validatePanel(desc) == VROOpenXRPanelError::InvalidDimensions,
           "infinite height");
    desc = quad("v");
    desc.pose.orientation = {0, 0, 0, 0};
    expect(compositor.validatePanel(desc) == VROOpenXRPanelError::InvalidPose, "zero quaternion");
    desc = quad("v");
    desc.shape = VROOpenXRPanelShape::Cylinder;
    expect(compositor.validatePanel(desc) == VROOpenXRPanelError::CylinderUnavailable,
           "cylinder without the extension");
    expect(compositor.validatePanel(quad("")) == VROOpenXRPanelError::EmptyId, "empty id");

    expect(compositor.addPanel(quad("a"), ok) == VROOpenXRPanelError::None, "add a");
    expect(compositor.validatePanel(quad("a")) == VROOpenXRPanelError::DuplicateId, "duplicate id");
    expect(compositor.addPanel(quad("b"), ok) == VROOpenXRPanelError::None, "add b");
    expect(compositor.validatePanel(quad("c")) == VROOpenXRPanelError::LayerBudgetExceeded,
           "budget full");

    // addPanel reports the same codes.
    desc = quad("d");
    desc.widthPixels = 8;
    expect(compositor.addPanel(desc, ok) == VROOpenXRPanelError::LayerBudgetExceeded,
           "addPanel checks the budget first, as before");
    compositor.removePanel("b");
    expect(compositor.addPanel(desc, ok) == VROOpenXRPanelError::InvalidPixelSize,
           "addPanel refuses 8 px");
    expect(compositor.addPanel(quad("e"), nullptr) == VROOpenXRPanelError::MissingProducer,
           "addPanel still needs a producer");
}

} // namespace

int main() {
    testBudgetEviction();
    testHiddenAndPendingPanels();
    testValidatePanel();
    // Every compositor above has been destroyed, so every swapchain it made.
    expect(gLoggedLivePanelSwapchains == 0, "logged panel swapchain count returns to 0");
    if (gFailures != 0) {
        std::fprintf(stderr, "panel eviction: %d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("panel eviction: %d checks passed\n", gChecks);
    return 0;
}
