//
//  VRORenderTargetMetalXR.mm
//
//  See header for purpose. bind() resolves the color/depth attachments
//  (external swapchain textures for display, owned textures offscreen),
//  builds a render pass descriptor, and opens a render command encoder on
//  the driver's frame command buffer.
//
//  SOT-KEYWORDS: metal, render-target, openxr, macos
//

#include "VRORenderTargetMetalXR.h"
#if VRO_METAL

#include "VRODriverMetal.h"
#include "VROTexture.h"

VRORenderTargetMetalXR::VRORenderTargetMetalXR(std::weak_ptr<VRODriverMetal> driver,
                                               VRORenderTargetType type, int numAttachments) :
    VRORenderTarget(type, numAttachments),
    _driver(driver),
    _externalColor(nil),
    _externalDepth(nil),
    _colorTexture(nil),
    _depthTexture(nil),
    _hydrated(false),
    _warnedStencil(false),
    _warnedUnsupported(false) {
}

VRORenderTargetMetalXR::~VRORenderTargetMetalXR() {
}

void VRORenderTargetMetalXR::setExternalTextures(id <MTLTexture> color, id <MTLTexture> depth) {
    _externalColor = color;
    _externalDepth = depth;
    _hydrated = (color != nil);
}

bool VRORenderTargetMetalXR::setViewport(VROViewport viewport) {
    bool changed = (_viewport.getWidth() != viewport.getWidth() ||
                    _viewport.getHeight() != viewport.getHeight() ||
                    _viewport.getX() != viewport.getX() ||
                    _viewport.getY() != viewport.getY());
    _viewport = viewport;
    if (changed && _type != VRORenderTargetType::Display) {
        // Offscreen attachments are sized to the viewport; force rehydration.
        _hydrated = false;
    }
    return changed;
}

bool VRORenderTargetMetalXR::hydrate() {
    if (_hydrated) {
        return true;
    }
    std::shared_ptr<VRODriverMetal> driver = _driver.lock();
    if (!driver) {
        return false;
    }
    int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) {
        pwarn("VRORenderTargetMetalXR: cannot hydrate with %dx%d viewport", w, h);
        return false;
    }

    MTLPixelFormat colorFormat = (_type == VRORenderTargetType::ColorTextureHDR16)
        ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
    MTLTextureDescriptor *cd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:colorFormat
        width:w height:h mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    cd.storageMode = MTLStorageModePrivate;
    _colorTexture = [driver->getDevice() newTextureWithDescriptor:cd];

    MTLTextureDescriptor *dd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:driver->getDepthPixelFormat()
        width:w height:h mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModePrivate;
    _depthTexture = [driver->getDevice() newTextureWithDescriptor:dd];

    _hydrated = (_colorTexture != nil);
    if (!_hydrated) {
        pwarn("VRORenderTargetMetalXR: texture allocation failed");
    }
    return _hydrated;
}

int VRORenderTargetMetalXR::getWidth() const {
    if (_externalColor) {
        return (int)_externalColor.width;
    }
    return _viewport.getWidth();
}

int VRORenderTargetMetalXR::getHeight() const {
    if (_externalColor) {
        return (int)_externalColor.height;
    }
    return _viewport.getHeight();
}

id <MTLTexture> VRORenderTargetMetalXR::colorTexture() const {
    return _externalColor ? _externalColor : _colorTexture;
}

id <MTLTexture> VRORenderTargetMetalXR::depthTexture() const {
    return _externalDepth ? _externalDepth : _depthTexture;
}

void VRORenderTargetMetalXR::bind() {
    std::shared_ptr<VRODriverMetal> driver = _driver.lock();
    if (!driver) {
        return;
    }
    if (!_hydrated && !hydrate()) {
        pwarn("VRORenderTargetMetalXR: bind() — hydrate failed, no encoder opened");
        return;
    }

    id <MTLTexture> color = colorTexture();
    if (color == nil) {
        pwarn("VRORenderTargetMetalXR: bind() — no color attachment");
        return;
    }

    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor =
        MTLClearColorMake(_clearColor.x, _clearColor.y, _clearColor.z, _clearColor.w);

    id <MTLTexture> depth = depthTexture();
    if (depth != nil) {
        rp.depthAttachment.texture = depth;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp.depthAttachment.clearDepth = 1.0;
    }

    id <MTLCommandBuffer> commandBuffer = driver->getFrameCommandBuffer();
    if (commandBuffer == nil) {
        pwarn("VRORenderTargetMetalXR: bind() — no frame command buffer");
        return;
    }

    id <MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:rp];
    [encoder setViewport:(MTLViewport){
        (double)_viewport.getX(), (double)_viewport.getY(),
        (double)_viewport.getWidth(), (double)_viewport.getHeight(), 0.0, 1.0 }];
    driver->setActiveRenderEncoder(encoder);
}

void VRORenderTargetMetalXR::bindRead() {
    // Texture reads are direct Metal API usage on this platform.
}

void VRORenderTargetMetalXR::invalidate() {
    // Tile-memory hint only; nothing to do — the owning command buffer's
    // store actions already describe the (lack of) writeback.
}

void VRORenderTargetMetalXR::blitColor(std::shared_ptr<VRORenderTarget> destination,
                                       bool flipY, std::shared_ptr<VRODriver> driver) {
    if (!_warnedUnsupported) {
        pwarn("VRORenderTargetMetalXR::blitColor unsupported — post-process chain incomplete");
        _warnedUnsupported = true;
    }
}
void VRORenderTargetMetalXR::blitStencil(std::shared_ptr<VRORenderTarget> destination,
                                         bool flipY, std::shared_ptr<VRODriver> driver) {
    if (!_warnedUnsupported) {
        pwarn("VRORenderTargetMetalXR::blitStencil unsupported");
        _warnedUnsupported = true;
    }
}
void VRORenderTargetMetalXR::blitDepth(std::shared_ptr<VRORenderTarget> destination) {
    if (!_warnedUnsupported) {
        pwarn("VRORenderTargetMetalXR::blitDepth unsupported");
        _warnedUnsupported = true;
    }
}

void VRORenderTargetMetalXR::deleteFramebuffers() {
    _colorTexture = nil;
    _depthTexture = nil;
    _hydrated = false;
}

bool VRORenderTargetMetalXR::restoreFramebuffers() {
    return hydrate();
}

bool VRORenderTargetMetalXR::hasTextureAttached(int attachment) {
    return attachment == 0 && colorTexture() != nil;
}

void VRORenderTargetMetalXR::clearTextures() {
    _colorTexture = nil;
}

bool VRORenderTargetMetalXR::attachNewTextures() {
    // Owned textures are allocated in hydrate(); nothing else to attach.
    return hydrate();
}

void VRORenderTargetMetalXR::setTextureImageIndex(int index, int attachment) {
    if (!_warnedUnsupported) {
        pwarn("VRORenderTargetMetalXR::setTextureImageIndex unsupported");
        _warnedUnsupported = true;
    }
}
void VRORenderTargetMetalXR::setTextureCubeFace(int face, int mipLevel, int attachmentIndex) {
    if (!_warnedUnsupported) {
        pwarn("VRORenderTargetMetalXR::setTextureCubeFace unsupported");
        _warnedUnsupported = true;
    }
}
void VRORenderTargetMetalXR::setMipLevel(int mipLevel, int attachmentIndex) {
    if (!_warnedUnsupported) {
        pwarn("VRORenderTargetMetalXR::setMipLevel unsupported");
        _warnedUnsupported = true;
    }
}
void VRORenderTargetMetalXR::attachTexture(std::shared_ptr<VROTexture> texture, int attachment) {
    if (!_warnedUnsupported) {
        pwarn("VRORenderTargetMetalXR::attachTexture unsupported (render-to-texture incomplete)");
        _warnedUnsupported = true;
    }
}
const std::shared_ptr<VROTexture> VRORenderTargetMetalXR::getTexture(int attachment) const {
    return nullptr;
}

void VRORenderTargetMetalXR::clearStencil() {}
void VRORenderTargetMetalXR::clearDepth() {}
void VRORenderTargetMetalXR::clearColor() {}
void VRORenderTargetMetalXR::clearDepthAndColor() {}

void VRORenderTargetMetalXR::enablePortalStencilWriting(VROFace face) {
    if (!_warnedStencil) {
        pwarn("VRORenderTargetMetalXR: portal stencil writes unsupported (portal frames will not clip)");
        _warnedStencil = true;
    }
}
void VRORenderTargetMetalXR::enablePortalStencilRemoval(VROFace face) {
    if (!_warnedStencil) {
        pwarn("VRORenderTargetMetalXR: portal stencil unsupported");
        _warnedStencil = true;
    }
}
void VRORenderTargetMetalXR::disablePortalStencilWriting(VROFace face) {}
void VRORenderTargetMetalXR::setPortalStencilPassFunction(VROFace face, VROStencilFunc func, int ref) {}

#endif // VRO_METAL
