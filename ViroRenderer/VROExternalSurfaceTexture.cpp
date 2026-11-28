//
//  VROExternalSurfaceTexture.cpp
//  ViroRenderer
//

#include "VROExternalSurfaceTexture.h"
#include "VROTextureSubstrate.h"
#include "VROFrameListener.h"
#include "VROFrameSynchronizer.h"
#include "VRONitroCanvasSource.h"
#include "VROLog.h"

// Per-platform hooks. Defined in:
//   iOS / visionOS: ios/ViroKit/VROExternalSurfaceTextureImpl.mm
//   Android:        android/sharedCode/src/main/cpp/VROExternalSurfaceTextureImpl.cpp
//
// importHandle returns nullptr if the handle cannot be imported (mismatched
// platform, malformed fields, allocation failure). Errors are logged inside
// the impl; the caller treats a null result as "skip this frame."
//
// consumeFence takes ownership of handle.fenceFd and makes the GPU (or, as a
// bounded fallback, the CPU) wait for the producer's write before anything
// samples the texture.
std::unique_ptr<VROTextureSubstrate>
VROExternalSurfaceTextureImpl_importHandle(const VROSharedTextureHandle &handle,
                                           std::shared_ptr<VRODriver> &driver);
void VROExternalSurfaceTextureImpl_consumeFence(const VROSharedTextureHandle &handle,
                                                std::shared_ptr<VRODriver> &driver);

namespace {

// On Android every import is a GL_TEXTURE_EXTERNAL_OES name; the type tells
// the shader factory to sample it through samplerExternalOES.
VROTextureType externalTextureType() {
#if VRO_PLATFORM_ANDROID
    return VROTextureType::TextureEGLImage;
#else
    return VROTextureType::Texture2D;
#endif
}

/*
 Frame-start hook for a texture in lookup mode. Holds the texture and the
 driver weakly: the frame synchronizer keeps only a weak reference to this
 listener, and the texture owns the listener, so neither keeps the other
 alive.
 */
class VROExternalSurfaceTextureFrameListener : public VROFrameListener {
public:
    VROExternalSurfaceTextureFrameListener(std::weak_ptr<VROExternalSurfaceTexture> texture,
                                           std::weak_ptr<VRODriver> driver) :
        _texture(std::move(texture)), _driver(std::move(driver)) {}

    void onFrameWillRender(const VRORenderContext &context) override {
        (void) context;
        std::shared_ptr<VROExternalSurfaceTexture> texture = _texture.lock();
        std::shared_ptr<VRODriver> driver = _driver.lock();
        if (texture && driver) {
            texture->updateFromNitroCanvasSurface(driver);
        }
    }
    void onFrameDidRender(const VRORenderContext &context) override {
        (void) context;
    }

private:
    std::weak_ptr<VROExternalSurfaceTexture> _texture;
    std::weak_ptr<VRODriver> _driver;
};

} // namespace

VROExternalSurfaceTexture::VROExternalSurfaceTexture(int width, int height, bool sRGB)
    : VROTexture(externalTextureType(), VROTextureInternalFormat::RGBA8),
      _width(width), _height(height), _sRGB(sRGB), _hasImportedStorage(false) {}

VROExternalSurfaceTexture::~VROExternalSurfaceTexture() = default;

std::shared_ptr<VROExternalSurfaceTexture>
VROExternalSurfaceTexture::createForNitroCanvasSurface(int32_t surfaceId, int width, int height,
                                                       bool sRGB) {
    std::shared_ptr<VROExternalSurfaceTexture> texture =
        std::make_shared<VROExternalSurfaceTexture>(width, height, sRGB);
    texture->_nitroSource.reset(new VRONitroCanvasSource(surfaceId));
    return texture;
}

void VROExternalSurfaceTexture::updateFromHandle(const VROSharedTextureHandle &handle,
                                                 std::shared_ptr<VRODriver> &driver) {
    auto substrate = VROExternalSurfaceTextureImpl_importHandle(handle, driver);
    if (!substrate) {
        return;
    }
    if (handle.width > 0 && handle.height > 0) {
        _width = handle.width;
        _height = handle.height;
    }
    setSubstrate(0, std::move(substrate));
}

bool VROExternalSurfaceTexture::updateFromNitroCanvasSurface(std::shared_ptr<VRODriver> &driver) {
    if (!_nitroSource || !driver) {
        return false;
    }
    VRONitroCanvasFrame frame;
    if (!_nitroSource->poll(&frame)) {
        return false;
    }

    // The fence guards the producer's write into storage we may already be
    // sampling, so it is consumed whether or not the storage is re-imported.
    VROExternalSurfaceTextureImpl_consumeFence(frame.handle, driver);
    frame.handle.fenceFd = -1;

    if (frame.storageChanged || !_hasImportedStorage) {
        std::unique_ptr<VROTextureSubstrate> substrate =
            VROExternalSurfaceTextureImpl_importHandle(frame.handle, driver);
        if (!substrate) {
            // Import again from scratch on the producer's next frame.
            _nitroSource->invalidateStorage();
            _hasImportedStorage = false;
            return false;
        }
        _width = frame.handle.width;
        _height = frame.handle.height;
        setSubstrate(0, std::move(substrate));
        _hasImportedStorage = true;
    }
    return true;
}

void VROExternalSurfaceTexture::attachToFrameSynchronizer(
        std::shared_ptr<VROFrameSynchronizer> frameSynchronizer,
        std::shared_ptr<VRODriver> driver) {
    if (!_nitroSource || !frameSynchronizer || !driver) {
        return;
    }
    detachFromFrameSynchronizer();
    std::weak_ptr<VROExternalSurfaceTexture> self =
        std::static_pointer_cast<VROExternalSurfaceTexture>(shared_from_this());
    _frameListener = std::make_shared<VROExternalSurfaceTextureFrameListener>(self, driver);
    frameSynchronizer->addFrameListener(_frameListener);
    _frameSynchronizer = frameSynchronizer;
}

void VROExternalSurfaceTexture::detachFromFrameSynchronizer() {
    std::shared_ptr<VROFrameSynchronizer> frameSynchronizer = _frameSynchronizer.lock();
    if (frameSynchronizer && _frameListener) {
        frameSynchronizer->removeFrameListener(_frameListener);
    }
    _frameListener.reset();
    _frameSynchronizer.reset();
}

int32_t VROExternalSurfaceTexture::getNitroCanvasSurfaceId() const {
    return _nitroSource ? _nitroSource->getSurfaceId() : 0;
}

uint64_t VROExternalSurfaceTexture::getNitroCanvasFrameCount() const {
    return _nitroSource ? _nitroSource->getFrameCount() : 0;
}
