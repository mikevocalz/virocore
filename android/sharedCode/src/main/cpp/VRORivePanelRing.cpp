//
//  VRORivePanelRing.cpp
//  ViroRenderer
//

#include "VRORivePanelRing.h"

#include "VRORivePanel.h"

#include <android/log.h>

#include <utility>

#define RIVE_PANEL_LOG_TAG "ViroRivePanel"

namespace {
// Upper bound for destroy() waiting on a consumer fence. The consumer
// flushed it in endRead(), so it signals as soon as that draw retires.
constexpr GLuint64 kConsumerFenceTimeoutNs = 100ull * 1000ull * 1000ull;
}

bool VRORivePanelRing::create(int widthPx, int heightPx, std::string *error) {
    destroy();
    std::array<Slot, kSlots> slots{};
    for (Slot &slot : slots) {
        glGenTextures(1, &slot.texture);
        glBindTexture(GL_TEXTURE_2D, slot.texture);
        // Immutable RGBA8: what TextureRenderTargetGL needs for every interlock
        // mode, and UNORM so Rive's sRGB-encoded bytes are stored as written.
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, widthPx, heightPx);
        // Object state, shared with the consumer: one level, linear so a
        // destination of a different size still filters.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        for (Slot &slot : slots) {
            if (slot.texture != 0) glDeleteTextures(1, &slot.texture);
        }
        *error = "ring texture allocation failed: GL error " + std::to_string(glError);
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _slots = slots;
    _latest = -1;
    _reading = -1;
    _width = widthPx;
    _height = heightPx;
    _alive = true;
    return true;
}

void VRORivePanelRing::destroy() {
    std::array<Slot, kSlots> slots{};
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (!_alive) return;
        _alive = false;
        // A read in progress is one bounded draw on the XR thread.
        _readDone.wait(lock, [this] { return _reading < 0; });
        slots = _slots;
        _slots = {};
        _latest = -1;
        _width = 0;
        _height = 0;
    }
    for (Slot &slot : slots) {
        if (slot.consumerFence != nullptr) {
            // The consumer's sampling draw must retire before the texture goes.
            const GLenum result = glClientWaitSync(slot.consumerFence, 0, kConsumerFenceTimeoutNs);
            if (result == GL_TIMEOUT_EXPIRED || result == GL_WAIT_FAILED) {
                __android_log_print(ANDROID_LOG_WARN, RIVE_PANEL_LOG_TAG,
                                    "ring: consumer fence did not signal (0x%x)", result);
            }
            glDeleteSync(slot.consumerFence);
        }
        if (slot.producerFence != nullptr) glDeleteSync(slot.producerFence);
        if (slot.texture != 0) glDeleteTextures(1, &slot.texture);
    }
}

bool VRORivePanelRing::ready() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _alive;
}

int VRORivePanelRing::beginWrite() {
    GLsync consumerFence = nullptr;
    GLsync staleProducerFence = nullptr;
    int slot = -1;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_alive) return -1;
        // Oldest slot that is neither the newest frame nor being read. With
        // three slots one always qualifies.
        for (int i = 0; i < kSlots; ++i) {
            if (i == _latest || i == _reading) continue;
            if (slot < 0 || _slots[i].sequence < _slots[slot].sequence) slot = i;
        }
        if (slot < 0) return -1;
        consumerFence = _slots[slot].consumerFence;
        _slots[slot].consumerFence = nullptr;
        // A frame that was published but superseded before anyone read it.
        // Nobody waits on it: beginRead only takes the newest slot's fence.
        staleProducerFence = _slots[slot].producerFence;
        _slots[slot].producerFence = nullptr;
    }
    if (consumerFence != nullptr) {
        glWaitSync(consumerFence, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(consumerFence);
    }
    if (staleProducerFence != nullptr) glDeleteSync(staleProducerFence);
    return slot;
}

GLuint VRORivePanelRing::texture(int slot) const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (slot < 0 || slot >= kSlots) return 0;
    return _slots[slot].texture;
}

uint64_t VRORivePanelRing::endWrite(int slot) {
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    // A fence another context waits on must have been flushed, or the wait
    // can stall forever.
    glFlush();
    uint64_t sequence = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_alive && slot >= 0 && slot < kSlots) {
            _slots[slot].producerFence = fence;
            _slots[slot].sequence = ++_sequence;
            _latest = slot;
            sequence = _sequence;
            fence = nullptr;
        }
    }
    if (fence != nullptr) glDeleteSync(fence);
    return sequence;
}

bool VRORivePanelRing::beginRead(VRORivePanelFrame *frame) {
    GLsync fence = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_alive || _latest < 0 || _reading >= 0) return false;
        _reading = _latest;
        const Slot &slot = _slots[_reading];
        frame->texture = slot.texture;
        frame->width = _width;
        frame->height = _height;
        frame->sequence = slot.sequence;
        // Taken once: a second read of the same frame needs no wait.
        fence = slot.producerFence;
        _slots[_reading].producerFence = nullptr;
    }
    if (fence != nullptr) {
        glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
        // Deletion is deferred by GL until the wait above is resolved.
        glDeleteSync(fence);
    }
    return true;
}

void VRORivePanelRing::endRead() {
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    GLsync previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_reading >= 0) {
            // A later fence on the same context implies the earlier one.
            previous = _slots[_reading].consumerFence;
            _slots[_reading].consumerFence = fence;
            fence = nullptr;
            _reading = -1;
        }
    }
    _readDone.notify_all();
    if (previous != nullptr) glDeleteSync(previous);
    if (fence != nullptr) glDeleteSync(fence);
}
