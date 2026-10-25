//
//  VRORivePanelRing.h
//  ViroRenderer
//
//  Three GL_RGBA8 textures that a VRORivePanel worker renders into and the
//  OpenXR render thread samples (ADR 0001 route B2). The worker's EGL context
//  is created sharing the XR render context, so the texture names and the
//  sync objects below are valid in both.
//
//  Ownership and threads
//  - Textures and fences are created and deleted by the worker, with its
//    context current. create(), destroy(), beginWrite() and endWrite() are
//    worker-only.
//  - beginRead() and endRead() run on the consumer thread (the XR render
//    thread) with a context of the same share group current.
//  - One mutex guards the slot table (latest, reading, fences). No GL call is
//    made under it.
//
//  Synchronisation
//  - Producer ready: endWrite() inserts a fence after the frame and flushes,
//    then publishes the slot as the newest. beginRead() takes that fence and
//    makes the consumer's GPU wait on it (glWaitSync), so sampling never
//    overtakes Rive's rendering. The CPU never blocks.
//  - Consumer complete: endRead() inserts a fence after the sampling draw and
//    flushes. beginWrite() never picks the newest or the slot being read, and
//    before rendering into a slot it makes the worker's GPU wait on that
//    slot's consumer fence, so Rive never overwrites pixels still being read.
//  - destroy() blocks until no read is in progress, waits on outstanding
//    consumer fences, then deletes. The consumer must not hold a frame across
//    a call that could destroy the ring (resize, dispose); the compositor's
//    producer call is a single bounded draw.
//
//  Pixels: premultiplied, sRGB-encoded bytes (what Rive's GL backend writes
//  into a UNORM target), rows bottom-up (TextureRenderTargetGL's default).
//

#pragma once

#include <GLES3/gl3.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

struct VRORivePanelFrame;

class __attribute__((visibility("hidden"))) VRORivePanelRing {
public:
    static constexpr int kSlots = 3;

    VRORivePanelRing() = default;
    ~VRORivePanelRing() = default;  // destroy() must already have run on the worker
    VRORivePanelRing(const VRORivePanelRing &) = delete;
    VRORivePanelRing &operator=(const VRORivePanelRing &) = delete;

    // ---- Worker, worker context current ----
    bool create(int widthPx, int heightPx, std::string *error);
    void destroy();
    bool ready() const;
    // Returns the slot to render into; its texture is safe to overwrite once
    // the worker's GL commands that follow run (the wait is server-side).
    int beginWrite();
    GLuint texture(int slot) const;
    // Fences the frame, flushes and publishes the slot as the newest.
    // Returns the frame's sequence number (1, 2, ...).
    uint64_t endWrite(int slot);

    // ---- Consumer, a share-group context current ----
    // False when nothing has been published yet or the ring is gone.
    bool beginRead(VRORivePanelFrame *frame);
    void endRead();

private:
    struct Slot {
        GLuint texture = 0;
        GLsync producerFence = nullptr;  // set by endWrite, taken by beginRead
        GLsync consumerFence = nullptr;  // set by endRead, taken by beginWrite
        uint64_t sequence = 0;
    };

    mutable std::mutex _mutex;
    std::condition_variable _readDone;
    std::array<Slot, kSlots> _slots{};
    int _latest = -1;
    int _reading = -1;
    bool _alive = false;
    int _width = 0;
    int _height = 0;
    uint64_t _sequence = 0;
};
