//
//  VRODriverMetalOpenXR.mm
//
//  See header for purpose.
//
//  SOT-KEYWORDS: metal, driver, openxr, macos
//

#include "VRODriverMetalOpenXR.h"
#if VRO_METAL

VRODriverMetalOpenXR::VRODriverMetalOpenXR(id <MTLDevice> device,
                                           id <MTLCommandQueue> commandQueue) :
    VRODriverMetal(device, commandQueue),
    _commandBuffer(nil),
    _initialized(false) {
    /*
     Report Adreno330OrOlder: the choreographer uses _mrtSupported to decide
     whether to create offscreen render targets and the blit/tone-mapping
     post-process chain. Reporting false collapses VROChoreographer to a
     direct "scene → display" render, which is the milestone scope. The GL
     shader-generation side effects of this GPU type are unreachable here
     (the Metal material substrate never touches VROShaderFactory).
     */
    _gpuType = VROGPUType::Adreno330OrOlder;
    pinfo("VRODriverMetalOpenXR: reporting VROGPUType::Adreno330OrOlder — "
          "MRT/HDR/bloom/shadow/post-process passes disabled (direct-to-display render)");
}

void VRODriverMetalOpenXR::initialize() {
    if (_initialized) {
        return;
    }
    _displayTarget = std::make_shared<VRORenderTargetMetalXR>(
        shared_from_this(), VRORenderTargetType::Display, 1);
    _initialized = true;
}

void VRODriverMetalOpenXR::beginEye(id <MTLTexture> color, id <MTLTexture> depth) {
    _commandBuffer = [_commandQueue commandBuffer];
    setFrameCommandBuffer(_commandBuffer);
    setColorPixelFormat(color.pixelFormat);

    std::shared_ptr<VRORenderTargetMetalXR> display =
        std::static_pointer_cast<VRORenderTargetMetalXR>(_displayTarget);
    display->setExternalTextures(color, depth);
}

void VRODriverMetalOpenXR::endEye(id <MTLTexture> readback) {
    unbindRenderTarget(); // ends the active encoder and clears the bound target

    if (readback != nil && _commandBuffer != nil) {
        std::shared_ptr<VRORenderTargetMetalXR> display =
            std::static_pointer_cast<VRORenderTargetMetalXR>(_displayTarget);
        id <MTLTexture> source = display->colorTexture();
        if (source != nil) {
            id <MTLBlitCommandEncoder> blit = [_commandBuffer blitCommandEncoder];
            [blit copyFromTexture:source toTexture:readback];
            [blit endEncoding];
        }
    }

    if (_commandBuffer != nil) {
        [_commandBuffer commit];
        [_commandBuffer waitUntilCompleted];
        if (_commandBuffer.status == MTLCommandBufferStatusError) {
            pwarn("VRODriverMetalOpenXR: command buffer error: %s",
                  _commandBuffer.error
                      ? [[_commandBuffer.error localizedDescription] UTF8String] : "?");
        }
        _commandBuffer = nil;
    }
    setFrameCommandBuffer(nil);
}

std::shared_ptr<VRORenderTarget> VRODriverMetalOpenXR::newRenderTarget(
        VRORenderTargetType type, int numAttachments, int numImages,
        bool enableMipmaps, bool needsDepthStencil) {
    if (numImages != 1 || enableMipmaps) {
        pwarn("VRODriverMetalOpenXR::newRenderTarget: numImages=%d mipmaps=%d unsupported — "
              "returning basic target", numImages, (int)enableMipmaps);
    }
    return std::make_shared<VRORenderTargetMetalXR>(shared_from_this(), type, numAttachments);
}

#endif // VRO_METAL
