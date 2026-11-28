#include "VRONitroCanvasLookup.h"
#include "VRONitroCanvasSource.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

// Host-only test of the nitro_canvas_lookup_v2 consumer. The "Nitro canvas
// lookup host test" step in .github/workflows/pull-request.yml builds it from
// the repository root with -std=c++14 -Wall -Wextra -Werror -rdynamic, so the
// nitro_canvas_lookup_v2 defined below is visible to dlsym(RTLD_DEFAULT) the
// way the producer library's export is on a device.

namespace {

// What the fake producer publishes next. ready == false means "no new frame".
struct FakeProducer {
    bool ready = false;
    NitroCanvasSharedTextureHandle payload{};
    int calls = 0;
    bool lastCallerStampOk = false;
};
FakeProducer gProducer;

NitroCanvasSharedTextureHandle makePayload(uint64_t ahb, int32_t w, int32_t h, int32_t fence) {
    NitroCanvasSharedTextureHandle p{};
    p.structSize = sizeof(p);
    p.abiVersion = NITRO_CANVAS_ABI_VERSION;
    p.width = w;
    p.height = h;
    p.flags = NITRO_CANVAS_FLAG_SRGB;
    p.ahardwareBuffer = ahb;
    p.fenceFd = fence;
    return p;
}

// Mirrors the producer's contract in cpp/CanvasInViroCore.cpp: refuse a
// caller that did not stamp size and version, hand a dirty frame out once.
int fakeLookup(int32_t surfaceId, NitroCanvasSharedTextureHandle *out) {
    ++gProducer.calls;
    gProducer.lastCallerStampOk = out->structSize == sizeof(NitroCanvasSharedTextureHandle) &&
                                  out->abiVersion == NITRO_CANVAS_ABI_VERSION &&
                                  out->width == 0 && out->ahardwareBuffer == 0;
    if (!gProducer.lastCallerStampOk || surfaceId != 7 || !gProducer.ready) {
        return 0;
    }
    *out = gProducer.payload;
    gProducer.ready = false;
    return 1;
}

int pipeReadEnd(int *writeEnd) {
    int fds[2];
    assert(pipe(fds) == 0);
    *writeEnd = fds[1];
    return fds[0];
}

bool isOpen(int fd) {
    return fcntl(fd, F_GETFD) != -1;
}

} // namespace

// The symbol the producer exports. Resolution must find it without any
// override, through dlsym(RTLD_DEFAULT).
extern "C" __attribute__((visibility("default")))
int nitro_canvas_lookup_v2(int32_t surfaceId, NitroCanvasSharedTextureHandle *out) {
    return fakeLookup(surfaceId, out);
}

int main() {
    static_assert(sizeof(NitroCanvasSharedTextureHandle) == 48, "ABI size");

    // 1. Lazy resolution through dlsym finds the exported symbol.
    assert(VRONitroCanvasLookup::isAvailable());
    NitroCanvasSharedTextureHandle out;
    assert(VRONitroCanvasLookup::lookup(7, &out) == VRONitroCanvasLookupResult::NoFrame);
    assert(gProducer.calls == 1);
    assert(gProducer.lastCallerStampOk); // caller zeroed and stamped the struct
    assert(out.fenceFd == -1);

    // 2. A published frame comes back once, then NoFrame.
    gProducer.payload = makePayload(0x1000, 256, 128, -1);
    gProducer.ready = true;
    assert(VRONitroCanvasLookup::lookup(7, &out) == VRONitroCanvasLookupResult::Frame);
    assert(out.ahardwareBuffer == 0x1000 && out.width == 256 && out.height == 128);
    assert(VRONitroCanvasLookup::lookup(7, &out) == VRONitroCanvasLookupResult::NoFrame);

    // 3. Validation: a wrong version, two backings, or a zero size is
    //    rejected, and the fence that came with it is closed.
    int writeEnd = -1;
    int fence = pipeReadEnd(&writeEnd);
    gProducer.payload = makePayload(0x1000, 256, 128, fence);
    gProducer.payload.abiVersion = 1;
    gProducer.ready = true;
    assert(VRONitroCanvasLookup::lookup(7, &out) == VRONitroCanvasLookupResult::Rejected);
#if defined(__APPLE__)
    // Apple producers put an MTLSharedEvent value here, not an FD: the
    // consumer must not close it.
    assert(isOpen(fence));
    close(fence);
#else
    assert(!isOpen(fence));
#endif
    assert(out.fenceFd == -1);
    close(writeEnd);

    NitroCanvasSharedTextureHandle two = makePayload(0x1000, 256, 128, -1);
    two.iosurface = 0x2000;
    assert(!VRONitroCanvasLookup::isValidHandle(two));
    NitroCanvasSharedTextureHandle empty = makePayload(0x1000, 0, 128, -1);
    assert(!VRONitroCanvasLookup::isValidHandle(empty));
    NitroCanvasSharedTextureHandle reserved = makePayload(0x1000, 256, 128, -1);
    reserved.reserved0 = 1;
    assert(!VRONitroCanvasLookup::isValidHandle(reserved));
    assert(VRONitroCanvasLookup::isValidHandle(makePayload(0x1000, 256, 128, -1)));

    // 4. The source reports storage changes only when the backing changes.
    VRONitroCanvasSource source(7);
    VRONitroCanvasFrame frame;
    assert(!source.poll(&frame)); // nothing published

    gProducer.payload = makePayload(0x1000, 256, 128, -1);
    gProducer.ready = true;
    assert(source.poll(&frame));
    assert(frame.storageChanged); // first frame always imports
    assert(!source.poll(&frame)); // consumed

    // Same AHardwareBuffer, new pixels, with a fence that moves to us.
    fence = pipeReadEnd(&writeEnd);
    gProducer.payload = makePayload(0x1000, 256, 128, fence);
    gProducer.ready = true;
    assert(source.poll(&frame));
    assert(!frame.storageChanged);
    assert(frame.handle.fenceFd == fence); // ownership passed, still open
    assert(isOpen(fence));
    close(fence);
    close(writeEnd);

    // Resize: new buffer and size.
    gProducer.payload = makePayload(0x3000, 512, 128, -1);
    gProducer.ready = true;
    assert(source.poll(&frame));
    assert(frame.storageChanged);
    assert(frame.handle.width == 512);

    // sRGB flag flip alone is a storage change.
    gProducer.payload = makePayload(0x3000, 512, 128, -1);
    gProducer.payload.flags = 0;
    gProducer.ready = true;
    assert(source.poll(&frame));
    assert(frame.storageChanged);

    // invalidateStorage() forces the next frame to import again.
    source.invalidateStorage();
    gProducer.payload = makePayload(0x3000, 512, 128, -1);
    gProducer.payload.flags = 0;
    gProducer.ready = true;
    assert(source.poll(&frame));
    assert(frame.storageChanged);
    assert(source.getFrameCount() == 5);

    // Two sources on one id: the producer hands the frame out once, both
    // see it, and only the one that took it gets the fence.
    {
        VRONitroCanvasSource first(7);
        VRONitroCanvasSource second(7);
        fence = pipeReadEnd(&writeEnd);
        gProducer.payload = makePayload(0x4000, 64, 64, fence);
        gProducer.ready = true;
        assert(first.poll(&frame));
        assert(frame.handle.ahardwareBuffer == 0x4000 && frame.handle.fenceFd == fence);
        assert(second.poll(&frame));
        assert(frame.handle.ahardwareBuffer == 0x4000 && frame.handle.fenceFd == -1);
        assert(!first.poll(&frame));
        assert(!second.poll(&frame));
        close(fence);
        close(writeEnd);
    }

    // A different surface id never sees this producer's frames.
    VRONitroCanvasSource other(8);
    gProducer.ready = true;
    assert(!other.poll(&frame));
    gProducer.ready = false;

    // 5. An override replaces resolution; clearing it resolves again.
    static int overrideCalls = 0;
    VRONitroCanvasLookup::setLookupFunctionForTesting(
        [](int32_t, NitroCanvasSharedTextureHandle *) -> int { ++overrideCalls; return 0; });
    assert(VRONitroCanvasLookup::lookup(7, &out) == VRONitroCanvasLookupResult::NoFrame);
    assert(overrideCalls == 1);
    VRONitroCanvasLookup::setLookupFunctionForTesting(nullptr);
    const int before = gProducer.calls;
    assert(VRONitroCanvasLookup::lookup(7, &out) == VRONitroCanvasLookupResult::NoFrame);
    assert(gProducer.calls == before + 1);

    std::printf("VRONitroCanvasSourceTest: all checks passed\n");
    return 0;
}
