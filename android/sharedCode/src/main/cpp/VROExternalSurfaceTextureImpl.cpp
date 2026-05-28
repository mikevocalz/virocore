//
//  VROExternalSurfaceTextureImpl.cpp
//  ViroRenderer (Android)
//
//  Route B: AHardwareBuffer -> EGLImage -> GL_TEXTURE_EXTERNAL_OES (API 26+).
//  Route A: pass-through of a SurfaceTexture-backed external-OES name owned
//  by the producer. Selected by which field of VROSharedTextureHandle is set.
//

#include "VROExternalSurfaceTexture.h"
#include "VROTextureSubstrateOpenGL.h"
#include "VRODriverOpenGL.h"
#include "VROLog.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#if __ANDROID_API__ >= 26
  #include <android/hardware_buffer.h>
#endif

namespace {

// Lazily-resolved EGL/GL extension function pointers. Resolved once on the
// first import call; nullptr if unavailable on the current driver.
struct EglFns {
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC getNativeClientBuffer = nullptr;
    PFNEGLCREATEIMAGEKHRPROC               createImage           = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC              destroyImage          = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC    imageTargetTexture2D  = nullptr;
    bool resolved = false;
};

EglFns &eglFns() {
    static EglFns fns;
    if (!fns.resolved) {
        fns.getNativeClientBuffer = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
            eglGetProcAddress("eglGetNativeClientBufferANDROID");
        fns.createImage = (PFNEGLCREATEIMAGEKHRPROC)
            eglGetProcAddress("eglCreateImageKHR");
        fns.destroyImage = (PFNEGLDESTROYIMAGEKHRPROC)
            eglGetProcAddress("eglDestroyImageKHR");
        fns.imageTargetTexture2D = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");
        fns.resolved = true;
    }
    return fns;
}

// Route B substrate: owns the EGLImage and the AHardwareBuffer ref, releases
// both on destruction. ownTexture=true so the parent destructor deletes the
// GL texture name we glGen-ed.
class VROExternalSurfaceSubstrateAHB : public VROTextureSubstrateOpenGL {
public:
    VROExternalSurfaceSubstrateAHB(GLuint name,
                                   std::shared_ptr<VRODriver> driver,
                                   EGLImageKHR image,
#if __ANDROID_API__ >= 26
                                   AHardwareBuffer *buffer
#else
                                   void *buffer
#endif
                                   )
        : VROTextureSubstrateOpenGL(GL_TEXTURE_EXTERNAL_OES, name, driver, /*ownTexture*/ true),
          _image(image), _buffer(buffer) {}

    ~VROExternalSurfaceSubstrateAHB() override {
        auto &fns = eglFns();
        if (_image != EGL_NO_IMAGE_KHR && fns.destroyImage) {
            fns.destroyImage(eglGetCurrentDisplay(), _image);
        }
#if __ANDROID_API__ >= 26
        if (_buffer) {
            AHardwareBuffer_release(_buffer);
        }
#endif
    }
private:
    EGLImageKHR _image;
#if __ANDROID_API__ >= 26
    AHardwareBuffer *_buffer;
#else
    void *_buffer;
#endif
};

#if __ANDROID_API__ >= 26
std::unique_ptr<VROTextureSubstrate>
importRouteB(const VROSharedTextureHandle &handle,
             std::shared_ptr<VRODriver> &driver) {
    auto &fns = eglFns();
    if (!fns.getNativeClientBuffer || !fns.createImage || !fns.imageTargetTexture2D) {
        pinfo("VROExternalSurfaceTexture: Route B unavailable, missing EGL extensions");
        return nullptr;
    }

    AHardwareBuffer *buf = reinterpret_cast<AHardwareBuffer *>(
        static_cast<uintptr_t>(handle.ahardwareBuffer));
    AHardwareBuffer_acquire(buf);

    EGLClientBuffer clientBuf = fns.getNativeClientBuffer(buf);
    if (!clientBuf) {
        AHardwareBuffer_release(buf);
        pinfo("VROExternalSurfaceTexture: eglGetNativeClientBufferANDROID returned null");
        return nullptr;
    }

    // EGL_IMAGE_PRESERVED preserves the buffer contents across the import; this
    // is what makes the producer's writes visible after the fence.
    const EGLint imageAttrs[] = {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE
    };
    EGLImageKHR image = fns.createImage(eglGetCurrentDisplay(), EGL_NO_CONTEXT,
                                        EGL_NATIVE_BUFFER_ANDROID, clientBuf, imageAttrs);
    if (image == EGL_NO_IMAGE_KHR) {
        AHardwareBuffer_release(buf);
        pinfo("VROExternalSurfaceTexture: eglCreateImageKHR failed: 0x%x", eglGetError());
        return nullptr;
    }

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texId);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    fns.imageTargetTexture2D(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)image);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    return std::make_unique<VROExternalSurfaceSubstrateAHB>(texId, driver, image, buf);
}
#endif // __ANDROID_API__ >= 26

// Route A: the producer already owns a SurfaceTexture-backed GL_TEXTURE_EXTERNAL_OES
// and calls updateTexImage() on it from onFrameAvailable. We wrap the existing
// GL name and tell the substrate not to delete it (ownTexture=false).
std::unique_ptr<VROTextureSubstrate>
importRouteA(const VROSharedTextureHandle &handle,
             std::shared_ptr<VRODriver> &driver) {
    GLuint texId = handle.surfaceTextureGLId;
    if (texId == 0) {
        pinfo("VROExternalSurfaceTexture: Route A handle missing surfaceTextureGLId");
        return nullptr;
    }
    return std::make_unique<VROTextureSubstrateOpenGL>(
        GL_TEXTURE_EXTERNAL_OES, texId, driver, /*ownTexture*/ false);
}

} // namespace

std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver) {
#if __ANDROID_API__ >= 26
    if (handle.ahardwareBuffer != 0) {
        return importRouteB(handle, driver);
    }
#endif
    if (handle.surfaceTextureGLId != 0) {
        return importRouteA(handle, driver);
    }
    pinfo("VROExternalSurfaceTexture: Android handle has no backing buffer set");
    return nullptr;
}
