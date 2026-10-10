#include "../../android/sharedCode/src/main/cpp/VROOpenXRPanelImageState.h"
#include <cassert>
#include <cstdio>

struct FakeSwapchain {
    int acquired = 0;
    int waited = 0;
    int drawn = 0;
    int released = 0;
    bool waitOk = true;
    bool drawOk = true;
    bool releaseOk = true;
    VROOpenXRPanelImageState state;

    void frame() {
        state.step(
            [&](uint32_t &index) { ++acquired; index = 0; return true; },
            [&] { ++waited; return waitOk; },
            [&](uint32_t index) { assert(index == 0); ++drawn; return drawOk; },
            [&] { ++released; assert(waitOk); return releaseOk; });
    }
};

int main() {
    // Wait timeout: the same acquired index is waited again, never released early.
    FakeSwapchain timeout;
    timeout.waitOk = false;
    timeout.frame();
    assert(timeout.acquired == 1 && timeout.waited == 1 && timeout.released == 0);
    assert(timeout.state.isAcquired() && !timeout.state.isWaited());
    assert(!timeout.state.hasReleasedImage());
    timeout.waitOk = true;
    timeout.frame();
    assert(timeout.acquired == 1 && timeout.waited == 2 && timeout.released == 1);
    assert(timeout.state.hasReleasedImage());
    timeout.frame();
    assert(timeout.acquired == 1 && timeout.drawn == 1 && timeout.released == 1);

    // Failed paint holds the waited image; prior released image stays visible.
    timeout.state.invalidate();
    timeout.drawOk = false;
    timeout.frame();
    assert(timeout.acquired == 2 && timeout.waited == 3 && timeout.released == 1);
    assert(timeout.state.hasReleasedImage() && timeout.state.isWaited());
    timeout.drawOk = true;
    timeout.frame();
    assert(timeout.acquired == 2 && timeout.waited == 3 && timeout.released == 2);

    // Release failure retries just release, not paint or wait.
    timeout.state.invalidate();
    timeout.releaseOk = false;
    timeout.frame();
    const int paints = timeout.drawn, waits = timeout.waited;
    assert(timeout.released == 3 && timeout.state.hasReleasedImage());
    timeout.releaseOk = true;
    timeout.frame();
    assert(timeout.drawn == paints && timeout.waited == waits && timeout.released == 4);

    // First frame producer failure must not publish an uninitialized layer.
    FakeSwapchain firstDraw;
    firstDraw.drawOk = false;
    firstDraw.frame();
    assert(!firstDraw.state.hasReleasedImage() && firstDraw.released == 0);
    firstDraw.drawOk = true;
    firstDraw.frame();
    assert(firstDraw.acquired == 1 && firstDraw.waited == 1 && firstDraw.released == 1);
    firstDraw.state.reset();
    assert(!firstDraw.state.isAcquired() && !firstDraw.state.hasReleasedImage());
    std::puts("VROOpenXRPanelImageState: 4 scenarios passed");
}
