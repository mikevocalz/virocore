//
//  VROTextureSubstrateMetal.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 12/4/15.
//  Copyright © 2015 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "VROTextureSubstrateMetal.h"
#if VRO_METAL

#include "VROImageUtil.h"
#include "VROTexture.h"
#include "VRODriverMetal.h"
#include "VROLog.h"
#include "VROImage.h"

VROTextureSubstrateMetal::VROTextureSubstrateMetal(VROTextureType type, std::vector<std::shared_ptr<VROImage>> &images,
                                                   std::shared_ptr<VRODriver> &driver) {
    
    VRODriverMetal &metal = (VRODriverMetal &)(*driver);
    id <MTLDevice> device = metal.getDevice();

    if (type == VROTextureType::Texture2D) {
        std::shared_ptr<VROImage> &image = images.front();
        int width = image->getWidth();
        int height = image->getHeight();
        
        size_t dataLength;
        unsigned char *data = image->getData(&dataLength);

        int bytesPerPixel = 4;
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                              width:width
                                                                                             height:height
                                                                                          mipmapped:YES];
        _texture = [device newTextureWithDescriptor:descriptor];
        
        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [_texture replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:bytesPerPixel * width];
        
        id <MTLCommandBuffer> textureCommandBuffer = [metal.getCommandQueue() commandBuffer];
        id<MTLBlitCommandEncoder> commandEncoder = [textureCommandBuffer blitCommandEncoder];
        [commandEncoder generateMipmapsForTexture:_texture];
        [commandEncoder endEncoding];
        [textureCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            // Mipmap generation completed
            // NOTE: If VROTextureSubstrateMetal is destroyed before this completes,
            // _texture may be released while GPU is still using it. To prevent this,
            // ensure proper synchronization in VROViewMetal dealloc.
        }];
        [textureCommandBuffer commit];
    }
    
    else if (type == VROTextureType::TextureCube && images.size() == 6) {
        passert_msg(images.size() == 6,
                    "Cube texture can only be created from exactly six images");
        
        std::shared_ptr<VROImage> &firstImage = images.front();
        const CGFloat cubeSize = firstImage->getWidth();
        
        const NSUInteger bytesPerPixel = 4;
        const NSUInteger bytesPerRow = bytesPerPixel * cubeSize;
        const NSUInteger bytesPerImage = bytesPerRow * cubeSize;
        
        MTLRegion region = MTLRegionMake2D(0, 0, cubeSize, cubeSize);
        
        MTLTextureDescriptor *textureDescriptor = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                        size:cubeSize
                                                                                                   mipmapped:NO];
        _texture = [device newTextureWithDescriptor:textureDescriptor];
        
        for (size_t slice = 0; slice < 6; ++slice) {
            std::shared_ptr<VROImage> &image = images[slice];
            
            size_t dataLength;
            unsigned char *data = image->getData(&dataLength);
            
            passert_msg(image->getWidth() == cubeSize && image->getHeight() == cubeSize,
                        "Cube map images must be square and uniformly-sized");
            
            [_texture replaceRegion:region
                        mipmapLevel:0
                              slice:slice
                          withBytes:data
                        bytesPerRow:bytesPerRow
                      bytesPerImage:bytesPerImage];
        }
    }
    
    else {
        pabort("Invalid texture images received, could not convert to Metal");
    }
    
    ALLOCATION_TRACKER_ADD(TextureSubstrates, 1);
}

VROTextureSubstrateMetal::VROTextureSubstrateMetal(VROTextureType type, VROTextureFormat format,
                                                   std::shared_ptr<VROData> data, int width, int height,
                                                   std::shared_ptr<VRODriver> &driver, bool sRGB) {
    
    if (format == VROTextureFormat::ETC2_RGBA8_EAC) {
        VRODriverMetal &metal = (VRODriverMetal &)(*driver);
        id <MTLDevice> device = metal.getDevice();
        
        if (type == VROTextureType::Texture2D) {
            int bytesPerRow = width / 4 * 8; // TODO This is not working
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatETC2_RGB8
                                                                                                  width:width
                                                                                                 height:height
                                                                                              mipmapped:NO];
            _texture = [device newTextureWithDescriptor:descriptor];
     
            MTLRegion region = MTLRegionMake2D(0, 0, width, height);
            [_texture replaceRegion:region mipmapLevel:0 withBytes:data->getData() bytesPerRow:bytesPerRow];
        }
        else {
            pabort();
        }
    }
    else if (format == VROTextureFormat::ASTC_4x4_LDR) {
        VRODriverMetal &metal = (VRODriverMetal &)(*driver);
        id <MTLDevice> device = metal.getDevice();
        
        if (type == VROTextureType::Texture2D) {
            int bytesPerRow = width / 4 * 16; // texels per row / block footprint (4) / * 16 (each ASTC block is 16 bytes)
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatASTC_4x4_LDR
                                                                                                  width:width
                                                                                                 height:height
                                                                                              mipmapped:NO];
            _texture = [device newTextureWithDescriptor:descriptor];
            
            MTLRegion region = MTLRegionMake2D(0, 0, width, height);
            [_texture replaceRegion:region mipmapLevel:0 withBytes:data->getData() bytesPerRow:bytesPerRow];
        }
        else {
            pabort();
        }
    }
    else if (format == VROTextureFormat::RGBA8 || format == VROTextureFormat::RGB8) {
        // RGB8 images (from VROImageiOS) are also stored as 4 bytes/pixel (RGBA layout),
        // so both formats upload identically as MTLPixelFormatRGBA8Unorm.
        id <MTLDevice> device = ((VRODriverMetal &)(*driver)).getDevice();

        int bytesPerPixel = 4;
        MTLPixelFormat pixelFormat = sRGB ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                                              width:width
                                                                                             height:height
                                                                                          mipmapped:NO];

        _texture = [device newTextureWithDescriptor:descriptor];

        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [_texture replaceRegion:region mipmapLevel:0 withBytes:data->getData() bytesPerRow:bytesPerPixel * width];
    }
    else {
        pabort();
    }
    
    ALLOCATION_TRACKER_ADD(TextureSubstrates, 1);
}

VROTextureSubstrateMetal::~VROTextureSubstrateMetal() {
    invalidateSampler();
    ALLOCATION_TRACKER_SUB(TextureSubstrates, 1);
}

namespace {

MTLSamplerAddressMode toMetalAddressMode(VROWrapMode mode) {
    switch (mode) {
        case VROWrapMode::Clamp:          return MTLSamplerAddressModeClampToEdge;
        case VROWrapMode::Repeat:         return MTLSamplerAddressModeRepeat;
        case VROWrapMode::Mirror:         return MTLSamplerAddressModeMirrorRepeat;
        // GL's default border colour is transparent black, which ClampToZero
        // gives on every Metal family (ClampToBorderColor is not on all iOS GPUs).
        case VROWrapMode::ClampToBorder:  return MTLSamplerAddressModeClampToZero;
    }
    return MTLSamplerAddressModeRepeat;
}

// None reads as Nearest, the same as VROTextureSubstrateOpenGL::convertMagFilter.
MTLSamplerMinMagFilter toMetalFilter(VROFilterMode mode) {
    return mode == VROFilterMode::Linear ? MTLSamplerMinMagFilterLinear
                                         : MTLSamplerMinMagFilterNearest;
}

// None means no mipmapping: the glTF loader returns it for the NEAREST and
// LINEAR min filters, and GL then samples level 0 only.
MTLSamplerMipFilter toMetalMipFilter(VROFilterMode mode) {
    switch (mode) {
        case VROFilterMode::None:    return MTLSamplerMipFilterNotMipmapped;
        case VROFilterMode::Nearest: return MTLSamplerMipFilterNearest;
        case VROFilterMode::Linear:  return MTLSamplerMipFilterLinear;
    }
    return MTLSamplerMipFilterLinear;
}

}  // namespace

void VROTextureSubstrateMetal::setSamplerModes(VROWrapMode wrapS, VROWrapMode wrapT,
                                               VROFilterMode minFilter, VROFilterMode magFilter,
                                               VROFilterMode mipFilter) {
    _wrapS = wrapS;
    _wrapT = wrapT;
    _minFilter = minFilter;
    _magFilter = magFilter;
    _mipFilter = mipFilter;
    invalidateSampler();
}

void VROTextureSubstrateMetal::updateWrapMode(VROWrapMode wrapModeS, VROWrapMode wrapModeT) {
    _wrapS = wrapModeS;
    _wrapT = wrapModeT;
    invalidateSampler();
}

void VROTextureSubstrateMetal::invalidateSampler() {
    [_sampler release];
    _sampler = nil;
}

id <MTLSamplerState> VROTextureSubstrateMetal::getSampler() {
    if (_sampler != nil || _texture == nil) {
        return _sampler;
    }
    MTLSamplerDescriptor *descriptor = [[MTLSamplerDescriptor alloc] init];
    descriptor.sAddressMode = toMetalAddressMode(_wrapS);
    descriptor.tAddressMode = toMetalAddressMode(_wrapT);
    // Cube maps sample with a direction; r only matters there.
    descriptor.rAddressMode = toMetalAddressMode(_wrapT);
    descriptor.minFilter = toMetalFilter(_minFilter);
    descriptor.magFilter = toMetalFilter(_magFilter);
    // A texture without mips ignores the mip filter, so this needs no check
    // against mipmapLevelCount.
    descriptor.mipFilter = toMetalMipFilter(_mipFilter);
    descriptor.normalizedCoordinates = YES;
    _sampler = [[_texture device] newSamplerStateWithDescriptor:descriptor];
    [descriptor release];
    return _sampler;
}

#endif

