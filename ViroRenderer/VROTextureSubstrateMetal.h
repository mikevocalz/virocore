//
//  VROTextureSubstrateMetal.h
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

#ifndef VROTextureSubstrateMetal_h
#define VROTextureSubstrateMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include "VROTextureSubstrate.h"
#include <Metal/Metal.h>
#include <vector>
#include <memory>

class VRODriverMetal;
class VROData;
enum class VROTextureType;
enum class VROTextureFormat;
enum class VROTextureInternalFormat;
enum class VROMipmapMode;
enum class VROWrapMode;
enum class VROFilterMode;

/*
 Minimal Metal texture substrate. Uploads 2D RGBA8 data into an MTLTexture;
 all other formats currently produce a null texture with a logged warning;
 materials then render their plain diffuse color.
 */
class VROTextureSubstrateMetal : public VROTextureSubstrate {

public:

    VROTextureSubstrateMetal(VROTextureType type,
                             VROTextureFormat format,
                             VROTextureInternalFormat internalFormat, bool sRGB,
                             VROMipmapMode mipmapMode,
                             std::vector<std::shared_ptr<VROData>> &data,
                             int width, int height, std::vector<uint32_t> mipSizes,
                             VROWrapMode wrapS, VROWrapMode wrapT,
                             VROFilterMode minFilter, VROFilterMode magFilter,
                             VROFilterMode mipFilter,
                             VRODriverMetal &driver);
    virtual ~VROTextureSubstrateMetal();

    void updateWrapMode(VROWrapMode wrapModeS, VROWrapMode wrapModeT) override;

    id <MTLTexture> getTexture() const {
        return _texture;
    }
    // Sampler built from the texture's wrap and filter modes.
    id <MTLSamplerState> getSampler() const {
        return _sampler;
    }

private:

    void buildSampler(VROWrapMode wrapS, VROWrapMode wrapT);

    id <MTLTexture> _texture;
    id <MTLSamplerState> _sampler;
    id <MTLDevice> _device;
    VROFilterMode _minFilter;
    VROFilterMode _magFilter;

};

#endif // VRO_METAL
#endif /* VROTextureSubstrateMetal_h */
