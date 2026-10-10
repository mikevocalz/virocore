#pragma once

#include <cstdint>
#include <utility>

// One OpenXR *texture* swapchain image state. This must not be used for
// XR_KHR_android_surface_swapchain, which has no acquire/wait/release API.
class VROOpenXRPanelImageState final {
public:
    void invalidate() { _dirty = true; }
    bool hasReleasedImage() const { return _hasReleasedImage; }
    bool isAcquired() const { return _acquired; }
    bool isWaited() const { return _waited; }
    void reset() { *this = {}; }

    // Callbacks return true only on XR_SUCCESS (not XR_TIMEOUT_EXPIRED).
    // A draw is not released unless it succeeded, preserving the previous
    // compositor image even when a producer leaves partially painted pixels.
    template <typename Acquire, typename Wait, typename Draw, typename Release>
    void step(Acquire &&acquire, Wait &&wait, Draw &&draw, Release &&release) {
        if (!_dirty && !_acquired) return;
        if (!_acquired) {
            uint32_t index = 0;
            if (!std::forward<Acquire>(acquire)(index)) return;
            _index = index;
            _acquired = true;
            _waited = false;
            _readyToRelease = false;
        }
        if (!_waited) {
            if (!std::forward<Wait>(wait)()) return;
            _waited = true;
        }
        if (!_readyToRelease) {
            // Consume only the invalidation that caused this draw. A producer
            // may invalidate again while drawing, or while release is retried.
            _dirty = false;
            if (!std::forward<Draw>(draw)(_index)) {
                _dirty = true;
                return;
            }
            _readyToRelease = true;
        }
        if (!std::forward<Release>(release)()) return;
        _acquired = false;
        _waited = false;
        _readyToRelease = false;
        _hasReleasedImage = true;
    }

private:
    bool _dirty = true;
    bool _acquired = false;
    bool _waited = false;
    bool _readyToRelease = false;
    bool _hasReleasedImage = false;
    uint32_t _index = 0;
};
