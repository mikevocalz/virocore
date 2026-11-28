//
//  VRONitroCanvasLookup.cpp
//  ViroRenderer
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license.
//

#include "VRONitroCanvasLookup.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(__EMSCRIPTEN__)
  #define VRO_NITRO_CANVAS_HAS_DLFCN 0
#else
  #define VRO_NITRO_CANVAS_HAS_DLFCN 1
  #include <dlfcn.h>
#endif

#if !defined(__APPLE__)
  #include <unistd.h>
#endif

#if defined(__ANDROID__)
  #include <android/log.h>
  #define VRO_NC_LOG(...) __android_log_print(ANDROID_LOG_INFO, "Viro", __VA_ARGS__)
#else
  // Plain stderr: this file is also built into host tests, where VROLog.h
  // would take the Android branch and need the NDK.
  #define VRO_NC_LOG(...) do { std::fprintf(stderr, "[Viro] " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#endif

namespace {

// The producer library image to dlopen when RTLD_DEFAULT does not see the
// symbol. Android's System.loadLibrary() opens libraries RTLD_LOCAL, so the
// symbol is never global there; on Apple platforms SwiftPM or CocoaPods may
// link the producer as a framework or statically into the app, and the
// second case is covered by RTLD_DEFAULT.
const char *const kProducerLibraries[] = {
#if defined(__ANDROID__)
    "libNitroCanvasInVision.so",
#elif defined(__APPLE__)
    "@rpath/NitroCanvasInVision.framework/NitroCanvasInVision",
    "@executable_path/Frameworks/NitroCanvasInVision.framework/NitroCanvasInVision",
#else
    "libNitroCanvasInVision.so",
#endif
};

// Longest gap, in calls, between two resolution attempts after a miss.
const uint32_t kMaxRetryInterval = 512;

std::atomic<NitroCanvasLookupFn> sResolved{nullptr};
std::atomic<bool> sOverridden{false};

std::mutex sResolveMutex;          // guards everything below
uint32_t sCallsUntilRetry = 0;     // 0: try on the next call
uint32_t sRetryInterval = 1;
bool sLoggedMiss = false;

NitroCanvasLookupFn resolveNow() {
#if VRO_NITRO_CANVAS_HAS_DLFCN
    void *symbol = dlsym(RTLD_DEFAULT, VRONitroCanvasLookup::symbolName());
    if (symbol != nullptr) {
        VRO_NC_LOG("VRONitroCanvasLookup: %s resolved from the global scope",
                   VRONitroCanvasLookup::symbolName());
        return reinterpret_cast<NitroCanvasLookupFn>(symbol);
    }
    for (const char *library : kProducerLibraries) {
        // Never dlclose()d: the function pointer has to outlive every texture
        // that holds it, which is the life of the process.
        void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            continue;
        }
        symbol = dlsym(handle, VRONitroCanvasLookup::symbolName());
        if (symbol != nullptr) {
            VRO_NC_LOG("VRONitroCanvasLookup: %s resolved from %s",
                       VRONitroCanvasLookup::symbolName(), library);
            return reinterpret_cast<NitroCanvasLookupFn>(symbol);
        }
    }
#endif
    return nullptr;
}

NitroCanvasLookupFn resolvedFunction() {
    NitroCanvasLookupFn fn = sResolved.load(std::memory_order_acquire);
    if (fn != nullptr || sOverridden.load(std::memory_order_acquire)) {
        return fn;
    }

    std::lock_guard<std::mutex> lock(sResolveMutex);
    fn = sResolved.load(std::memory_order_relaxed);
    if (fn != nullptr) {
        return fn;
    }
    if (sCallsUntilRetry > 0) {
        --sCallsUntilRetry;
        return nullptr;
    }

    fn = resolveNow();
    if (fn != nullptr) {
        sResolved.store(fn, std::memory_order_release);
        return fn;
    }

    if (!sLoggedMiss) {
        sLoggedMiss = true;
        VRO_NC_LOG("VRONitroCanvasLookup: %s not found yet; is nitro-canvas-in-Vision "
                   "(ABI v%u) installed? Retrying on a backoff.",
                   VRONitroCanvasLookup::symbolName(), (unsigned) NITRO_CANVAS_ABI_VERSION);
    }
    sCallsUntilRetry = sRetryInterval;
    if (sRetryInterval < kMaxRetryInterval) {
        sRetryInterval *= 2;
    }
    return nullptr;
}

// Release a fence the caller will never see. Android FDs are closed; on Apple
// platforms the field is an MTLSharedEvent value and owns nothing.
void discardFence(int32_t fence) {
#if !defined(__APPLE__)
    if (fence >= 0) {
        ::close(fence);
    }
#else
    (void) fence;
#endif
}

} // namespace

bool VRONitroCanvasLookup::isValidHandle(const NitroCanvasSharedTextureHandle &handle) {
    if (handle.structSize != sizeof(NitroCanvasSharedTextureHandle) ||
        handle.abiVersion != NITRO_CANVAS_ABI_VERSION ||
        handle.reserved0 != 0) {
        return false;
    }
    if (handle.width <= 0 || handle.height <= 0) {
        return false;
    }
    const int backings = (handle.iosurface != 0 ? 1 : 0) +
                         (handle.ahardwareBuffer != 0 ? 1 : 0) +
                         (handle.surfaceTextureGLId != 0 ? 1 : 0);
    return backings == 1;
}

VRONitroCanvasLookupResult VRONitroCanvasLookup::lookup(int32_t surfaceId,
                                                        NitroCanvasSharedTextureHandle *out) {
    if (out == nullptr) {
        return VRONitroCanvasLookupResult::Rejected;
    }
    std::memset(out, 0, sizeof(*out));
    out->structSize = sizeof(NitroCanvasSharedTextureHandle);
    out->abiVersion = NITRO_CANVAS_ABI_VERSION;
    out->fenceFd = -1;

    NitroCanvasLookupFn fn = resolvedFunction();
    if (fn == nullptr) {
        return VRONitroCanvasLookupResult::Unavailable;
    }
    if (fn(surfaceId, out) == 0) {
        // The contract says nothing else was written. Reset fenceFd anyway so
        // a misbehaving producer cannot make the caller close a random FD.
        out->fenceFd = -1;
        return VRONitroCanvasLookupResult::NoFrame;
    }
    if (!isValidHandle(*out)) {
        // Only close what is plausibly an FD the producer moved to us; the
        // field is ours either way once the call returned success.
        discardFence(out->fenceFd);
        out->fenceFd = -1;
        return VRONitroCanvasLookupResult::Rejected;
    }
    return VRONitroCanvasLookupResult::Frame;
}

bool VRONitroCanvasLookup::isAvailable() {
    return resolvedFunction() != nullptr;
}

void VRONitroCanvasLookup::setLookupFunctionForTesting(NitroCanvasLookupFn fn) {
    std::lock_guard<std::mutex> lock(sResolveMutex);
    sResolved.store(fn, std::memory_order_release);
    sOverridden.store(fn != nullptr, std::memory_order_release);
    sCallsUntilRetry = 0;
    sRetryInterval = 1;
    sLoggedMiss = false;
}
