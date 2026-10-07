//
//  VRODriverMetalOpenXR.h
//
//  Concrete VRODriver for the desktop Metal/OpenXR path. Extends
//  VRODriverMetal with:
//    - a display render target bound to the per-eye OpenXR swapchain texture
//    - per-eye command-buffer lifecycle (beginEye/endEye)
//    - feature reporting that collapses the VROChoreographer to a direct
//      scene → display render (see getGPUType note)
//
//  SOT-KEYWORDS: metal, driver, openxr, macos
//

#ifndef VRODriverMetalOpenXR_h
#define VRODriverMetalOpenXR_h

#include "VRODefines.h"
#if VRO_METAL

#include "VRODriverMetal.h"
#include "VRORenderTargetMetalXR.h"
#include <Metal/Metal.h>

class VRODriverMetalOpenXR : public VRODriverMetal {

public:

    VRODriverMetalOpenXR(id <MTLDevice> device, id <MTLCommandQueue> commandQueue);
    virtual ~VRODriverMetalOpenXR() {}

    /*
     Must be invoked once after shared_ptr construction (needs
     shared_from_this to give the render targets a back-reference).
     */
    void initialize();

    /*
     Per-eye frame plumbing. beginEye installs the swapchain textures on the
     display target and opens a fresh command buffer; the actual render
     encoder is opened when the choreographer binds the display target.
     endEye ends the encoder, optionally blit-copies color into `readback`,
     then commits and waits.
     */
    void beginEye(id <MTLTexture> color, id <MTLTexture> depth);
    void endEye(id <MTLTexture> readback);

    std::shared_ptr<VRORenderTarget> newRenderTarget(VRORenderTargetType type, int numAttachments,
                                                     int numImages, bool enableMipmaps,
                                                     bool needsDepthStencil) override;

private:

    id <MTLCommandBuffer> _commandBuffer;
    bool _initialized;

};

#endif // VRO_METAL
#endif /* VRODriverMetalOpenXR_h */
