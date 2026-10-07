//
//  VRODriverMetalOpenXR.mm
//
//  See header for purpose.
//
//  SOT-KEYWORDS: metal, driver, openxr, macos
//

#include "VRODriverMetalOpenXR.h"
#if VRO_METAL

#include "VROLog.h"

VRODriverMetalOpenXR::VRODriverMetalOpenXR(id <MTLDevice> device) :
    VRODriverVisionOS(device) {
}

VRODriverMetalOpenXR::~VRODriverMetalOpenXR() {
    [_commandBuffer release];
}

void VRODriverMetalOpenXR::beginEye(id <MTLTexture> color, id <MTLTexture> depth) {
    // Pipelines are built against the driver's colour format, so it has to match
    // the swapchain image the runtime picked.
    setColorPixelFormat(color.pixelFormat);
    if (depth) {
        setDepthPixelFormat(depth.pixelFormat);
    }

    _commandBuffer = [[getCommandQueue() commandBuffer] retain];
    setFrameCommandBuffer(_commandBuffer);
    _eyeColor = color;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(_clearColor.x, _clearColor.y, _clearColor.z, _clearColor.w);
    if (depth) {
        pass.depthAttachment.texture = depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
    }
    beginDisplayPass(pass);
}

void VRODriverMetalOpenXR::endEye(id <MTLTexture> readback) {
    endDisplayPass();

    if (_commandBuffer == nil) {
        return;
    }
    if (readback != nil && _eyeColor != nil) {
        id <MTLBlitCommandEncoder> blit = [_commandBuffer blitCommandEncoder];
        [blit copyFromTexture:_eyeColor toTexture:readback];
        [blit endEncoding];
    }
    [_commandBuffer commit];
    [_commandBuffer waitUntilCompleted];
    if (_commandBuffer.status == MTLCommandBufferStatusError) {
        pwarn("VRODriverMetalOpenXR: command buffer error: %s",
              _commandBuffer.error
                  ? [[_commandBuffer.error localizedDescription] UTF8String] : "?");
    }
    [_commandBuffer release];
    _commandBuffer = nil;
    _eyeColor = nil;
    setFrameCommandBuffer(nil);
}

#endif // VRO_METAL
