//
//  VROAHardwareBufferImage.cpp
//  ViroRenderer (Android)
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license.
//

#include "VROAHardwareBufferImage.h"
#include "VROLog.h"

#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <mutex>

// The library is built for minSdk 24 and AHardwareBuffer arrived in API 26,
// so the NDK declares its functions unavailable at compile time. They are
// looked up in libandroid.so at runtime instead; on API 24-25 the lookup
// fails and the import reports it. The type itself is the global one that
// <EGL/eglext.h> forward-declares.
struct AHardwareBuffer;

namespace {

typedef void (*AHBAcquireFn)(AHardwareBuffer *);
typedef void (*AHBReleaseFn)(AHardwareBuffer *);

struct ImportFns {
    AHBAcquireFn acquire = nullptr;
    AHBReleaseFn release = nullptr;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC getNativeClientBuffer = nullptr;
    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetTexture2D = nullptr;
    PFNEGLCREATESYNCKHRPROC createSync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroySync = nullptr;
    PFNEGLWAITSYNCKHRPROC waitSync = nullptr;
};

const ImportFns &importFns() {
    static ImportFns fns;
    static std::once_flag once;
    std::call_once(once, [] {
        void *android = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (android != nullptr) {
            fns.acquire = (AHBAcquireFn) dlsym(android, "AHardwareBuffer_acquire");
            fns.release = (AHBReleaseFn) dlsym(android, "AHardwareBuffer_release");
        }
        fns.getNativeClientBuffer = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
            eglGetProcAddress("eglGetNativeClientBufferANDROID");
        fns.createImage = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
        fns.destroyImage = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
        fns.imageTargetTexture2D = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");
        fns.createSync = (PFNEGLCREATESYNCKHRPROC) eglGetProcAddress("eglCreateSyncKHR");
        fns.destroySync = (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress("eglDestroySyncKHR");
        fns.waitSync = (PFNEGLWAITSYNCKHRPROC) eglGetProcAddress("eglWaitSyncKHR");
    });
    return fns;
}

bool hasEGLExtension(EGLDisplay display, const char *name) {
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (extensions == nullptr) {
        return false;
    }
    const size_t length = strlen(name);
    for (const char *p = strstr(extensions, name); p != nullptr; p = strstr(p + length, name)) {
        const bool startOk = p == extensions || p[-1] == ' ';
        const bool endOk = p[length] == ' ' || p[length] == '\0';
        if (startOk && endOk) {
            return true;
        }
    }
    return false;
}

// Bound on the CPU fallback wait. One frame at 72 Hz is ~14 ms; a producer
// fence older than that is late already.
const int kMaxCpuWaitMs = 16;

} // namespace

std::unique_ptr<VROAHardwareBufferImage> VROAHardwareBufferImage::create(uint64_t buffer) {
    if (buffer == 0) {
        return nullptr;
    }
    const ImportFns &fns = importFns();
    if (!fns.acquire || !fns.release) {
        pinfo("VROAHardwareBufferImage: AHardwareBuffer needs Android 8.0 (API 26)");
        return nullptr;
    }
    if (!fns.getNativeClientBuffer || !fns.createImage || !fns.destroyImage ||
        !fns.imageTargetTexture2D) {
        pinfo("VROAHardwareBufferImage: EGL_ANDROID_get_native_client_buffer or "
              "EGL_KHR_image_base unavailable");
        return nullptr;
    }
    EGLDisplay display = eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY) {
        pinfo("VROAHardwareBufferImage: no current EGL display");
        return nullptr;
    }

    // Take our reference first: the pointer is the producer's, and it may
    // drop or replace the buffer on its own thread at any time.
    AHardwareBuffer *ahb = reinterpret_cast<AHardwareBuffer *>(static_cast<uintptr_t>(buffer));
    fns.acquire(ahb);

    EGLClientBuffer clientBuffer = fns.getNativeClientBuffer(ahb);
    if (clientBuffer == nullptr) {
        pinfo("VROAHardwareBufferImage: eglGetNativeClientBufferANDROID returned null (0x%x)",
              eglGetError());
        fns.release(ahb);
        return nullptr;
    }

    // Preserved: the image shows what the producer already wrote.
    const EGLint attributes[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    EGLImageKHR image = fns.createImage(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                        clientBuffer, attributes);
    if (image == EGL_NO_IMAGE_KHR) {
        pinfo("VROAHardwareBufferImage: eglCreateImageKHR failed (0x%x)", eglGetError());
        fns.release(ahb);
        return nullptr;
    }

    // The reference taken above is released in the destructor, after the image.
    return std::unique_ptr<VROAHardwareBufferImage>(
        new VROAHardwareBufferImage(buffer, display, image));
}

VROAHardwareBufferImage::VROAHardwareBufferImage(uint64_t buffer, EGLDisplay display,
                                                 EGLImageKHR image) :
    _buffer(buffer), _display(display), _image(image) {
}

VROAHardwareBufferImage::~VROAHardwareBufferImage() {
    const ImportFns &fns = importFns();
    if (_image != EGL_NO_IMAGE_KHR && fns.destroyImage) {
        fns.destroyImage(_display, _image);
    }
    if (fns.release) {
        fns.release(reinterpret_cast<AHardwareBuffer *>(static_cast<uintptr_t>(_buffer)));
    }
}

bool VROAHardwareBufferImage::bindTo(GLuint textureName) const {
    const ImportFns &fns = importFns();
    if (textureName == 0 || !fns.imageTargetTexture2D) {
        return false;
    }
    // Drop errors left by earlier GL work so they are not blamed on this bind.
    while (glGetError() != GL_NO_ERROR) {
    }
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureName);
    fns.imageTargetTexture2D(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES) _image);
    const GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    if (error != GL_NO_ERROR) {
        pinfo("VROAHardwareBufferImage: glEGLImageTargetTexture2DOES failed (0x%x)", error);
        return false;
    }
    return true;
}

void VROConsumeNativeFence(int fenceFd) {
    if (fenceFd < 0) {
        return;
    }
    const ImportFns &fns = importFns();
    EGLDisplay display = eglGetCurrentDisplay();
    if (display != EGL_NO_DISPLAY && fns.createSync && fns.destroySync && fns.waitSync &&
        hasEGLExtension(display, "EGL_ANDROID_native_fence_sync") &&
        hasEGLExtension(display, "EGL_KHR_wait_sync")) {
        const EGLint attributes[] = { EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fenceFd, EGL_NONE };
        EGLSyncKHR sync = fns.createSync(display, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
        if (sync != EGL_NO_SYNC_KHR) {
            // EGL owns the FD now. The wait is queued on the GPU: commands
            // issued after it run once the producer's write completes.
            fns.waitSync(display, sync, 0);
            fns.destroySync(display, sync);
            return;
        }
        // On failure the FD stays ours; fall through to the CPU wait.
        pinfo("VROConsumeNativeFence: eglCreateSyncKHR failed (0x%x), waiting on the CPU",
              eglGetError());
    }

    struct pollfd pfd;
    pfd.fd = fenceFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int result;
    do {
        result = poll(&pfd, 1, kMaxCpuWaitMs);
    } while (result < 0 && (errno == EINTR || errno == EAGAIN));
    if (result == 0) {
        pinfo("VROConsumeNativeFence: producer fence not signalled after %d ms; sampling anyway",
              kMaxCpuWaitMs);
    }
    close(fenceFd);
}
