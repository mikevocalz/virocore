//
//  VROAHardwareBufferImage.h
//  ViroRenderer (Android)
//
//  GL import of a producer's AHardwareBuffer, and the producer's sync fence,
//  for textures fed through the nitro-canvas-in-Vision lookup
//  (VROExternalSurfaceTexture, VROAndroidViewTexture in lookup mode).
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license.
//

#ifndef VROAHardwareBufferImage_h
#define VROAHardwareBufferImage_h

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdint>
#include <memory>

/*
 An EGLImage made from an AHardwareBuffer, holding its own reference on the
 buffer so the producer can drop or reallocate its copy at any time.

 create() runs on a thread with a current EGL context (the rendering thread).
 The destructor may run on any thread: eglDestroyImageKHR is a display-level
 call, and the display is the one recorded at creation.

 The image aliases the buffer's memory, so one import serves every frame the
 producer writes into the same buffer; bindTo() attaches it to an
 external-OES texture name.
 */
class VROAHardwareBufferImage {
public:
    /*
     Import `buffer` (an AHardwareBuffer* carried as an integer). Returns null
     when the EGL/GL extensions are missing or the import fails; the reason is
     logged.
     */
    static std::unique_ptr<VROAHardwareBufferImage> create(uint64_t buffer);
    ~VROAHardwareBufferImage();

    VROAHardwareBufferImage(const VROAHardwareBufferImage &) = delete;
    VROAHardwareBufferImage &operator=(const VROAHardwareBufferImage &) = delete;

    /*
     glEGLImageTargetTexture2DOES on `textureName` (GL_TEXTURE_EXTERNAL_OES).
     Leaves GL_TEXTURE_EXTERNAL_OES bound to 0. Rendering thread.
     */
    bool bindTo(GLuint textureName) const;

    uint64_t getBuffer() const { return _buffer; }

private:
    VROAHardwareBufferImage(uint64_t buffer, EGLDisplay display, EGLImageKHR image);

    uint64_t _buffer;
    EGLDisplay _display;
    EGLImageKHR _image;
};

/*
 Consume a producer fence before sampling what it guards. Takes ownership of
 `fenceFd` in every case; -1 means "no fence" and returns at once.

 With EGL_ANDROID_native_fence_sync and EGL_KHR_wait_sync the wait goes onto
 the GPU (eglWaitSyncKHR), so the rendering thread does not block. Without
 them it falls back to a CPU wait on the FD, bounded by kMaxCpuWaitMs; a
 producer that misses that bound is sampled anyway and the miss is logged,
 because holding the render thread longer drops headset frames. Rendering
 thread, EGL context current.
 */
void VROConsumeNativeFence(int fenceFd);

#endif /* VROAHardwareBufferImage_h */
