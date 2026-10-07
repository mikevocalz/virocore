//
//  VROTextureSubstrateMetal.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 12/8/15.
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
//

#include "VROTextureSubstrateMetal.h"
#if VRO_METAL

#include "VRODriverMetal.h"
#include "VROTexture.h"
#include "VROData.h"
#include "VROLog.h"

VROTextureSubstrateMetal::VROTextureSubstrateMetal(VROTextureType type,
                                                 VROTextureFormat format,
                                                 VROTextureInternalFormat internalFormat, bool sRGB,
                                                 VROMipmapMode mipmapMode,
                                                 std::vector<std::shared_ptr<VROData>> &data,
                                                 int width, int height,
                                                 std::vector<uint32_t> mipSizes,
                                                 VROWrapMode wrapS, VROWrapMode wrapT,
                                                 VROFilterMode minFilter, VROFilterMode magFilter,
                                                 VROFilterMode mipFilter,
                                                 VRODriverMetal &driver) :
    _texture(nil) {
    if (type != VROTextureType::Texture2D || data.empty() || data[0] == nullptr) {
        pwarn("VROTextureSubstrateMetal: only flat 2D textures with data are supported "
              "(type %d, images %zu) — texture will sample black", (int)type, data.size());
        return;
    }
    if (width <= 0 || height <= 0) {
        pwarn("VROTextureSubstrateMetal: bad dimensions %dx%d", width, height);
        return;
    }

    MTLPixelFormat pixelFormat;
    int bytesPerPixel;
    switch (format) {
        case VROTextureFormat::RGBA8:
            // sRGB data decodes to linear on sample, matching the GL path.
            pixelFormat = sRGB ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
            bytesPerPixel = 4;
            break;
        case VROTextureFormat::R8:
            pixelFormat = MTLPixelFormatR8Unorm;
            bytesPerPixel = 1;
            break;
        case VROTextureFormat::R32F:
            pixelFormat = MTLPixelFormatR32Float;
            bytesPerPixel = 4;
            break;
        default:
            pwarn("VROTextureSubstrateMetal: unsupported texture format %d — texture will sample black",
                  (int)format);
            return;
    }

    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:pixelFormat
        width:width height:height mipmapped:(mipmapMode != VROMipmapMode::None)];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    _texture = [driver.getDevice() newTextureWithDescriptor:desc];
    if (_texture == nil) {
        pwarn("VROTextureSubstrateMetal: MTLTexture allocation failed");
        return;
    }

    [_texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                mipmapLevel:0
                  withBytes:data[0]->getData()
                bytesPerRow:width * bytesPerPixel];
}

VROTextureSubstrateMetal::~VROTextureSubstrateMetal() {
    _texture = nil;
}

#endif // VRO_METAL
