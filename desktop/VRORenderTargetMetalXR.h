//
//  VRORenderTargetMetalXR.h
//
//  VRORenderTarget implementation for the desktop Metal/OpenXR driver.
//  Wraps a per-eye OpenXR swapchain texture (display) or a privately owned
//  Metal texture (offscreen targets created via newRenderTarget).
//
//  bind() opens a MTLRenderCommandEncoder on the driver's frame command
//  buffer — Metal allows only one encoder per command buffer, so the driver
//  ends the previously active encoder before binding the next target.
//
//  SOT-KEYWORDS: metal, render-target, openxr, macos
//

#ifndef VRORenderTargetMetalXR_h
#define VRORenderTargetMetalXR_h

#include "VRODefines.h"
#if VRO_METAL

#include "VRORenderTarget.h"
#include <Metal/Metal.h>
#include <memory>

class VRODriverMetal;

class VRORenderTargetMetalXR : public VRORenderTarget {

public:

    VRORenderTargetMetalXR(std::weak_ptr<VRODriverMetal> driver,
                           VRORenderTargetType type, int numAttachments);
    virtual ~VRORenderTargetMetalXR();

    /*
     For Display targets: install the swapchain textures for the eye about
     to be rendered. The target does not own them.
     */
    void setExternalTextures(id <MTLTexture> color, id <MTLTexture> depth);

    // VRORenderTarget
    bool setViewport(VROViewport viewport) override;
    bool hydrate() override;
    int getWidth() const override;
    int getHeight() const override;

    void bind() override;
    void bindRead() override;
    void invalidate() override;

    void blitColor(std::shared_ptr<VRORenderTarget> destination, bool flipY,
                   std::shared_ptr<VRODriver> driver) override;
    void blitStencil(std::shared_ptr<VRORenderTarget> destination, bool flipY,
                     std::shared_ptr<VRODriver> driver) override;
    void blitDepth(std::shared_ptr<VRORenderTarget> destination) override;

    void deleteFramebuffers() override;
    bool restoreFramebuffers() override;

    bool hasTextureAttached(int attachment) override;
    void clearTextures() override;
    bool attachNewTextures() override;
    void setTextureImageIndex(int index, int attachment) override;
    void setTextureCubeFace(int face, int mipLevel, int attachmentIndex) override;
    void setMipLevel(int mipLevel, int attachmentIndex) override;
    void attachTexture(std::shared_ptr<VROTexture> texture, int attachment) override;
    const std::shared_ptr<VROTexture> getTexture(int attachment) const override;

    void clearStencil() override;
    void clearDepth() override;
    void clearColor() override;
    void clearDepthAndColor() override;

    // Portal stencil support: not implemented (no portal frames in the
    // milestone scene); logged once per method so usage is visible.
    void enablePortalStencilWriting(VROFace face) override;
    void enablePortalStencilRemoval(VROFace face) override;
    void disablePortalStencilWriting(VROFace face) override;
    void setPortalStencilPassFunction(VROFace face, VROStencilFunc func, int ref) override;

    // Currently bound color/depth attachment (external or owned).
    id <MTLTexture> colorTexture() const;
    id <MTLTexture> depthTexture() const;

private:

    std::weak_ptr<VRODriverMetal> _driver;
    VROViewport _viewport;

    // Externally owned (swapchain) attachments for display targets.
    id <MTLTexture> _externalColor;
    id <MTLTexture> _externalDepth;

    // Owned attachments for offscreen targets.
    id <MTLTexture> _colorTexture;
    id <MTLTexture> _depthTexture;

    bool _hydrated;
    bool _warnedStencil;
    bool _warnedUnsupported;

};

#endif // VRO_METAL
#endif /* VRORenderTargetMetalXR_h */
