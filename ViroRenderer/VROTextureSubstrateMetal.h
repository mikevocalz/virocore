//
//  VROTextureSubstrateMetal.h
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

#ifndef VROTextureSubstrateMetal_h
#define VROTextureSubstrateMetal_h

#include "VRODefines.h"
#if VRO_METAL && defined(__OBJC__)

#include <Metal/Metal.h>
#include <vector>
#include "VROTextureSubstrate.h"
#include "VROTexture.h"
#include "VROAllocationTracker.h"

enum class VROTextureType;
enum class VROTextureFormat;
class VRODriver;
class VROData;
class VROImage;

class VROTextureSubstrateMetal : public VROTextureSubstrate {
    
public:
    
    /*
     Create a new texture substrate with the given underlying MTLTexture.
     */
    VROTextureSubstrateMetal(id <MTLTexture> texture) :
        _texture(texture) {
    
        ALLOCATION_TRACKER_ADD(TextureSubstrates, 1);
    }
    
    /*
     Create a new Metal texture of the given type from the given images.
     */
    VROTextureSubstrateMetal(VROTextureType type, std::vector<std::shared_ptr<VROImage>> &images,
                             std::shared_ptr<VRODriver> &driver);
    
    /*
     Create a new Metal texture out of the given format, with the given width, and height.
     When sRGB is true, RGBA8/RGB8 data is stored in an *_sRGB pixel format so
     sampling returns linear values, matching GL_SRGB8_ALPHA8 on the GL path.
     */
    VROTextureSubstrateMetal(VROTextureType type, VROTextureFormat format,
                             std::shared_ptr<VROData> data, int width, int height,
                             std::shared_ptr<VRODriver> &driver, bool sRGB = false);
    virtual ~VROTextureSubstrateMetal();
    
    id <MTLTexture> getTexture() const {
        return _texture;
    }
    void setTexture(id <MTLTexture> texture) {
        _texture = texture;
    }

    /*
     Sampling parameters from the owning VROTexture. Substrates created
     without one (render targets, video frames, glyph atlases) keep the
     defaults below, which match the shader-constant sampler every material
     texture used before samplers were per texture.
     */
    void setSamplerModes(VROWrapMode wrapS, VROWrapMode wrapT,
                         VROFilterMode minFilter, VROFilterMode magFilter,
                         VROFilterMode mipFilter);

    void updateWrapMode(VROWrapMode wrapModeS, VROWrapMode wrapModeT) override;

    /*
     The sampler for this texture, built on first use and rebuilt after a
     wrap or filter change. The geometry substrate binds it at the same slot
     as the texture ([[ sampler(n) ]] next to [[ texture(n) ]]). Nil while the
     substrate has no MTLTexture; callers bind the blank texture instead.

     Render thread only, like updateWrapMode and setSamplerModes: the cached
     sampler is released on invalidation without a lock. VROTexture calls
     updateWrapMode only once hydrated, and the GL substrate already issues GL
     calls there, so the render-thread contract is the existing one.
     */
    id <MTLSamplerState> getSampler();

private:
  
    id <MTLTexture> _texture;
    id <MTLSamplerState> _sampler = nil;

    VROWrapMode _wrapS = VROWrapMode::Repeat;
    VROWrapMode _wrapT = VROWrapMode::Repeat;
    VROFilterMode _minFilter = VROFilterMode::Linear;
    VROFilterMode _magFilter = VROFilterMode::Linear;
    VROFilterMode _mipFilter = VROFilterMode::Linear;

    void invalidateSampler();
    
};

#endif
#endif /* VROTextureSubstrateMetal_h */
