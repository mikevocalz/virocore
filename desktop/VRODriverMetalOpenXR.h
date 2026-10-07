//
//  VRODriverMetalOpenXR.h
//
//  VRODriver for the desktop OpenXR host. It is the visionOS Metal driver
//  (VRODriverVisionOS: render targets, post-process, Metal render passes)
//  with per-eye frame plumbing for OpenXR swapchain images: the runtime hands
//  over one colour texture per eye, the host owns the depth texture, and each
//  eye is encoded into its own command buffer.
//
//  SOT-KEYWORDS: metal, driver, openxr, macos
//

#ifndef VRODriverMetalOpenXR_h
#define VRODriverMetalOpenXR_h

#include "VRODefines.h"
#if VRO_METAL

#include "VRODriverVisionOS.h"
#include "VROVector4f.h"
#include <Metal/Metal.h>

class VRODriverMetalOpenXR : public VRODriverVisionOS {

public:

    explicit VRODriverMetalOpenXR(id <MTLDevice> device);
    virtual ~VRODriverMetalOpenXR();

    /*
     Clear colour for the display pass. Metal clears when a pass begins, so the
     colour has to be on the pass descriptor rather than issued as a command.
     */
    void setDisplayClearColor(VROVector4f color) { _clearColor = color; }

    /*
     Open a command buffer for one eye and give the display target a pass over
     the swapchain image (colour) and the host's depth texture. The encoder
     itself opens when the choreographer binds the display target.
     */
    void beginEye(id <MTLTexture> color, id <MTLTexture> depth);

    /*
     End the eye: close the display pass, blit the colour image into
     `readback` when it is non-nil, then commit and wait. Waiting keeps the
     swapchain image valid until xrReleaseSwapchainImage.
     */
    void endEye(id <MTLTexture> readback);

private:

    id <MTLCommandBuffer> _commandBuffer = nil;
    id <MTLTexture> _eyeColor = nil;
    VROVector4f _clearColor = VROVector4f(0, 0, 0, 1);

};

#endif // VRO_METAL
#endif /* VRODriverMetalOpenXR_h */
