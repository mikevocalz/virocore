//
//  VROExternalSurfaceTexture.cpp
//  ViroRenderer
//

#include "VROExternalSurfaceTexture.h"
#include "VROTextureSubstrate.h"

// Per-platform substrate factory. Defined in:
//   iOS / visionOS: ios/ViroKit/VROExternalSurfaceTextureImpl.mm
//   Android:        android/sharedCode/src/main/cpp/VROExternalSurfaceTextureImpl.cpp
//
// Implementations return nullptr if the handle cannot be imported (mismatched
// platform, malformed fields, allocation failure). Errors are logged inside
// the impl; the caller treats a null result as "skip this frame."
std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver);

VROExternalSurfaceTexture::VROExternalSurfaceTexture(int width, int height, bool sRGB)
    : VROTexture(VROTextureType::Texture2D, VROTextureInternalFormat::RGBA8),
      _width(width), _height(height), _sRGB(sRGB) {}

VROExternalSurfaceTexture::~VROExternalSurfaceTexture() = default;

void VROExternalSurfaceTexture::updateFromHandle(const VROSharedTextureHandle &handle,
                                                 std::shared_ptr<VRODriver> &driver) {
    auto substrate = VROExternalSurfaceTextureImpl_importHandle(handle, driver);
    if (!substrate) {
        return;
    }
    setSubstrate(0, std::move(substrate));
}
