//
//  VROExternalSurfaceTextureImpl.cpp
//  ViroRenderer (Android)
//
//  Route B: AHardwareBuffer -> EGLImage -> GL_TEXTURE_EXTERNAL_OES (API 26+,
//  resolved at runtime so the minSdk 24 library still loads on older devices).
//  Route A: pass-through of a SurfaceTexture-backed external-OES name owned
//  by the producer. Selected by which field of VROSharedTextureHandle is set.
//

#include "VROExternalSurfaceTexture.h"
#include "VROAHardwareBufferImage.h"
#include "VROTextureSubstrateOpenGL.h"
#include "VRODriverOpenGL.h"
#include "VROPanelGLStateGuard.h"
#include "VROLog.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

namespace {

// Route B substrate: owns the GL texture name (ownTexture=true, deleted by
// the parent destructor) and the EGLImage, which holds its own reference on
// the AHardwareBuffer.
class VROExternalSurfaceSubstrateAHB : public VROTextureSubstrateOpenGL {
public:
    VROExternalSurfaceSubstrateAHB(GLuint name,
                                   std::shared_ptr<VRODriver> driver,
                                   std::unique_ptr<VROAHardwareBufferImage> image)
        : VROTextureSubstrateOpenGL(GL_TEXTURE_EXTERNAL_OES, name,
                                    std::dynamic_pointer_cast<VRODriverOpenGL>(driver),
                                    /*owned*/ true),
          _image(std::move(image)) {}

private:
    std::unique_ptr<VROAHardwareBufferImage> _image;
};

std::unique_ptr<VROTextureSubstrate>
importRouteB(const VROSharedTextureHandle &handle,
             std::shared_ptr<VRODriver> &driver) {
    std::unique_ptr<VROAHardwareBufferImage> image =
        VROAHardwareBufferImage::create(handle.ahardwareBuffer);
    if (!image) {
        return nullptr;
    }

    // Runs at frame start, outside any draw: restore the bindings the
    // driver's state cache believes in.
    VROPanelGLStateGuard guard;
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texId);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    if (!image->bindTo(texId)) {
        glDeleteTextures(1, &texId);
        return nullptr;
    }
    return std::unique_ptr<VROTextureSubstrate>(
        new VROExternalSurfaceSubstrateAHB(texId, driver, std::move(image)));
}

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
    return std::unique_ptr<VROTextureSubstrate>(new VROTextureSubstrateOpenGL(
        GL_TEXTURE_EXTERNAL_OES, texId,
        std::dynamic_pointer_cast<VRODriverOpenGL>(driver),
        /*owned*/ false));
}

} // namespace

std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver) {
    if (handle.ahardwareBuffer != 0) {
        return importRouteB(handle, driver);
    }
    if (handle.surfaceTextureGLId != 0) {
        return importRouteA(handle, driver);
    }
    pinfo("VROExternalSurfaceTexture: Android handle has no backing buffer set");
    return nullptr;
}

void VROExternalSurfaceTextureImpl_consumeFence(const VROSharedTextureHandle &handle,
                                                std::shared_ptr<VRODriver> &driver) {
    (void) driver;
    VROConsumeNativeFence(handle.fenceFd);
}
