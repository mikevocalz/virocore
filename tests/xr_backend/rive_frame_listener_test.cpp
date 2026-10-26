// Host test for VROMakeRiveFrameListener (VROOpenXRRiveFrameListener.h).
// Rebuilds the ownership loop of a Rive compositor panel with stand-ins:
// FakePanel owns the frame listener the way VRORivePanel::Impl does, and
// FakeLink owns a fallback that owns the panel, like VRORiveCompositorLink
// and VROOpenXRRivePanelFallback. Dropping the renderer's reference to the
// link must free the link, the fallback and the panel. Run by
// scripts/test-rive-frame-listener.sh.

#include "VROOpenXRRiveFrameListener.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

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

struct FakePanel {
    std::function<void(uint64_t)> frameListener;
};

struct FakeFallback {
    std::shared_ptr<FakePanel> panel;
};

struct FakeLink {
    std::shared_ptr<FakeFallback> fallback;
    int renderTasks = 0;
};

// The render thread's queue.
std::vector<std::function<void()>> gQueue;

void drain() {
    std::vector<std::function<void()>> tasks;
    tasks.swap(gQueue);
    for (auto &task : tasks) task();
}

std::function<void(uint64_t)> listenerFor(const std::shared_ptr<FakeLink> &link) {
    return VROMakeRiveFrameListener(
        std::weak_ptr<FakeLink>(link),
        [](std::function<void()> task) { gQueue.push_back(std::move(task)); },
        [](FakeLink &live) { ++live.renderTasks; });
}

void testCycleIsBroken() {
    std::weak_ptr<FakeLink> weakLink;
    std::weak_ptr<FakePanel> weakPanel;
    {
        auto link = std::make_shared<FakeLink>();
        auto panel = std::make_shared<FakePanel>();
        panel->frameListener = listenerFor(link);
        link->fallback = std::make_shared<FakeFallback>();
        link->fallback->panel = panel;
        weakLink = link;
        weakPanel = panel;
        // The renderer's map entry (link) and the panel handle go away.
    }
    expect(weakLink.expired(), "link freed once the renderer drops it");
    expect(weakPanel.expired(), "panel freed with the fallback that held it");
}

void testCoalescing() {
    auto link = std::make_shared<FakeLink>();
    std::function<void(uint64_t)> onFrame = listenerFor(link);
    onFrame(1);
    onFrame(2);
    onFrame(3);
    expect(gQueue.size() == 1, "one task in flight per panel");
    drain();
    expect(link->renderTasks == 1, "the task ran with the live link");
    onFrame(4);
    expect(gQueue.size() == 1, "a frame after the task ran posts again");
    drain();
    expect(link->renderTasks == 2, "second task ran");
}

void testTaskAfterRemoval() {
    auto link = std::make_shared<FakeLink>();
    std::function<void(uint64_t)> onFrame = listenerFor(link);
    std::weak_ptr<FakeLink> weakLink = link;
    onFrame(1);
    link.reset();  // removeCompositorPanel erases the map entry
    expect(weakLink.expired(), "a queued task does not keep the link alive");
    drain();  // must not touch the freed link
    onFrame(2);
    expect(gQueue.size() == 1, "the listener still posts after removal");
    drain();
    expect(gQueue.empty(), "and its task is a no-op");
}

} // namespace

int main() {
    testCycleIsBroken();
    testCoalescing();
    testTaskAfterRemoval();
    if (gFailures != 0) {
        std::fprintf(stderr, "rive frame listener: %d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("rive frame listener: %d checks passed\n", gChecks);
    return 0;
}
