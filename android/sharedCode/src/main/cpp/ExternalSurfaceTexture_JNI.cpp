//
//  ExternalSurfaceTexture_JNI.cpp
//  ViroRenderer
//
//  JNI surface for com.viro.core.ExternalSurfaceTexture. Reuses the
//  VROAndroidViewTexture C++ class verbatim: it generates a
//  GL_TEXTURE_EXTERNAL_OES texture on the rendering thread, wraps it in a
//  VROTextureSubstrateOpenGL, creates a VideoSink (SurfaceTexture + Surface,
//  registered as a per-frame FrameListener that drives updateTexImage()), and
//  calls back setVideoSink(Surface) on the owning Java object. The callback is
//  resolved via GetObjectClass, so it binds to ExternalSurfaceTexture just as
//  it does to AndroidViewTexture.
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license, same
//  terms as VROAndroidViewTexture.cpp.

#include <capi/ViroContext_JNI.h>
#include "VROAndroidViewTexture.h"
#include "VROPlatformUtil.h"
#include "VRODriverOpenGL.h"

#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_ExternalSurfaceTexture_##method_name

extern "C" {

VRO_METHOD(VRO_REF(VROAndroidViewTexture), nativeCreateExternalSurfaceTexture)(VRO_ARGS
                                                                               VRO_REF(ViroContext) context_j,
                                                                               VRO_INT width,
                                                                               VRO_INT height) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<ViroContext> context_w = VRO_REF_GET(ViroContext, context_j);
    std::shared_ptr<VROAndroidViewTexture> texture
            = std::make_shared<VROAndroidViewTexture>(obj, width, height);

    VROPlatformDispatchAsyncRenderer([texture, context_w] {
        std::shared_ptr<ViroContext> context = context_w.lock();
        if (!context) {
            return;
        }
        texture->init(std::dynamic_pointer_cast<VRODriverOpenGL>(context->getDriver()));
    });

    return VRO_REF_NEW(VROAndroidViewTexture, texture);
}

VRO_METHOD(void, nativeDeleteExternalSurfaceTexture)(VRO_ARGS
                                                     VRO_REF(VROAndroidViewTexture) textureRef) {
    VRO_REF_DELETE(VROAndroidViewTexture, textureRef);
}

} // extern "C"
