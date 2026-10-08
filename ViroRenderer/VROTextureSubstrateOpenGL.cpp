//
//  VROTextureSubstrateOpenGL.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 5/2/16.
//  Copyright © 2016 Viro Media. All rights reserved.
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

#include "VROTextureSubstrateOpenGL.h"
#include "VROTexture.h"
#include "VROData.h"
#include "VRODriverOpenGL.h"
#include "VROLog.h"
#include <algorithm>

VROTextureSubstrateOpenGL::VROTextureSubstrateOpenGL(VROTextureType type,
                                                     VROTextureFormat format,
                                                     VROTextureInternalFormat internalFormat, bool sRGB,
                                                     VROMipmapMode mipmapMode,
                                                     std::vector<std::shared_ptr<VROData>> &data,
                                                     int width, int height,
                                                     const std::vector<uint32_t> &mipSizes,
                                                     VROWrapMode wrapS, VROWrapMode wrapT,
                                                     VROFilterMode minFilter, VROFilterMode magFilter, VROFilterMode mipFilter,
                                                     std::shared_ptr<VRODriverOpenGL> driver) :
    _owned(true),
    _driver(driver) {
    
    bool linearRenderingEnabled = driver->isLinearRenderingEnabled();
    driver->setActiveTextureUnit(GL_TEXTURE0);
    loadTexture(type, format, internalFormat, linearRenderingEnabled && sRGB, mipmapMode, data, width, height, mipSizes,
                wrapS, wrapT, minFilter, magFilter, mipFilter);
    ALLOCATION_TRACKER_ADD(TextureSubstrates, 1);
}

VROTextureSubstrateOpenGL::~VROTextureSubstrateOpenGL() {
    ALLOCATION_TRACKER_SUB(TextureSubstrates, 1);

    std::shared_ptr<VRODriverOpenGL> driver = _driver.lock();
    if (_owned && driver) {
        driver->deleteTexture(_texture);
    }
}

void VROTextureSubstrateOpenGL::updateWrapMode(VROWrapMode wrapModeS, VROWrapMode wrapModeT) {
    GL( glActiveTexture(GL_TEXTURE0) );
    GL( glBindTexture(_target, _texture) );
    GL( glTexParameteri(_target, GL_TEXTURE_WRAP_S, convertWrapMode(wrapModeS)) );
    GL( glTexParameteri(_target, GL_TEXTURE_WRAP_T, convertWrapMode(wrapModeT)) );
    GL( glBindTexture(_target, 0) );
}

void VROTextureSubstrateOpenGL::updateR32FData(const float *data, int width, int height) {
    GL( glActiveTexture(GL_TEXTURE0) );
    GL( glBindTexture(_target, _texture) );
    GL( glTexSubImage2D(_target, 0, 0, 0, width, height, GL_RED, GL_FLOAT, data) );
    GL( glBindTexture(_target, 0) );
}

void VROTextureSubstrateOpenGL::loadTexture(VROTextureType type,
                                            VROTextureFormat format,
                                            VROTextureInternalFormat internalFormat, bool sRGB,
                                            VROMipmapMode mipmapMode,
                                            std::vector<std::shared_ptr<VROData>> &data,
                                            int width, int height,
                                            const std::vector<uint32_t> &mipSizes,
                                            VROWrapMode wrapS, VROWrapMode wrapT,
                                            VROFilterMode minFilter, VROFilterMode magFilter, VROFilterMode mipFilter) {
 
    _target = GL_TEXTURE_2D;
    
    GL( glGenTextures(1, &_texture) );
    
    if (type == VROTextureType::Texture2D) {
        GL( glBindTexture(GL_TEXTURE_2D, _texture) );
        
        GL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, convertMinFilter(mipmapMode, minFilter, mipFilter)) );
        GL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, convertMagFilter(magFilter)) );
        GL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, convertWrapMode(wrapS)) );
        GL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, convertWrapMode(wrapT)) );
        
        loadFace(GL_TEXTURE_2D, format, internalFormat, sRGB,
                 mipmapMode, data.front(), width, height, mipSizes);
    }
    else if (type == VROTextureType::TextureCube) {
        passert_msg (mipmapMode == VROMipmapMode::None,
                     "Cube textures should not be mipmapped!");
        passert_msg (data.size() == 6,
                     "Cube textures can only be created from exactly six images");
        
        _target = GL_TEXTURE_CUBE_MAP;
        
        GL( glBindTexture(GL_TEXTURE_CUBE_MAP, _texture) );
        GL( glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, convertMinFilter(mipmapMode, minFilter, mipFilter)) );
        GL( glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, convertMagFilter(magFilter)) );
        GL( glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE) );
        GL( glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE) );
        GL( glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE) );
        
        for (int slice = 0; slice < 6; ++slice) {
            loadFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X + slice, format, internalFormat, sRGB,
                     mipmapMode, data[slice], width, height, mipSizes);
        }
    }
    else {
        pabort("Invalid texture data received, could not convert to OpenGL");
    }
}

void VROTextureSubstrateOpenGL::loadFace(GLenum target,
                                         VROTextureFormat format,
                                         VROTextureInternalFormat internalFormat,
                                         bool sRGB,
                                         VROMipmapMode mipmapMode,
                                         std::shared_ptr<VROData> &faceData,
                                         int width, int height,
                                         const std::vector<uint32_t> &mipSizes) {
    
    if (format == VROTextureFormat::ETC2_RGBA8_EAC || format == VROTextureFormat::ASTC_4x4_LDR) {
        passert (mipmapMode != VROMipmapMode::Runtime);
        GLenum compressedFormat;
        if (format == VROTextureFormat::ETC2_RGBA8_EAC) {
            compressedFormat = sRGB ? GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC : GL_COMPRESSED_RGBA8_ETC2_EAC;
        } else {
            compressedFormat = sRGB ? GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR : GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        }
        loadCompressedFace(target, compressedFormat, mipmapMode, faceData, width, height, mipSizes);
    }
    else if (format == VROTextureFormat::RGBA8 || format == VROTextureFormat::RGB8) {
        // We write format RGB8 into internal format RGBA8, because sRGB8 does not work
        // with automatic mipmap generation (it is not guaranteed color renderable)
        passert (mipmapMode != VROMipmapMode::Pregenerated);
        passert_msg (internalFormat != VROTextureInternalFormat::RGB565,
                     "RGB565 internal format requires RGB565 or RGB8 source data!");
        
        GL( glTexImage2D(target, 0, getInternalFormat(internalFormat, sRGB), width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, faceData->getData()) );
        if (mipmapMode == VROMipmapMode::Runtime) {
            GL( glGenerateMipmap(GL_TEXTURE_2D) );
        }
    }
    else if (format == VROTextureFormat::RGB9_E5) {
        // RGB9_E5 is not color renderable so automatic mipmap generation is not
        // supported
        passert (mipmapMode == VROMipmapMode::None);
        passert_msg (internalFormat == VROTextureInternalFormat::RGB9_E5,
                     "RGB9_E5 internal format requires RGB9_E5 source data!");
        
        GL( glTexImage2D(target, 0, GL_RGB9_E5, width, height, 0,
                         GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, faceData->getData()) );
    }
    else if (format == VROTextureFormat::RGB16F) {
        passert_msg (internalFormat == VROTextureInternalFormat::RGB16F,
                     "RGB16F internal format requires RGB16F source data!");
        
        GL( glTexImage2D(target, 0, GL_RGB16F, width, height, 0,
                         GL_RGB, GL_FLOAT, faceData->getData()) );
    }
    else if (format == VROTextureFormat::RGB565) {
        passert_msg (internalFormat == VROTextureInternalFormat::RGB565,
                     "RGB565 source format is only compatible with RGB565 internal format!");

        GL( glTexImage2D(target, 0, getInternalFormat(internalFormat, sRGB), width, height, 0,
                         GL_RGB, GL_UNSIGNED_SHORT_5_6_5, faceData->getData()) );
        if (mipmapMode == VROMipmapMode::Runtime) {
            GL( glGenerateMipmap(GL_TEXTURE_2D) );
        }
    }
    else if (format == VROTextureFormat::R32F) {
        // Single-channel 32-bit float texture (for depth maps)
        passert_msg (internalFormat == VROTextureInternalFormat::R32F,
                     "R32F source format is only compatible with R32F internal format!");
        passert (mipmapMode == VROMipmapMode::None);

        GL( glTexImage2D(target, 0, GL_R32F, width, height, 0,
                         GL_RED, GL_FLOAT, faceData->getData()) );
    }
    else if (format == VROTextureFormat::R8) {
        // Single-channel 8-bit unsigned texture (for confidence maps)
        passert_msg (internalFormat == VROTextureInternalFormat::R8,
                     "R8 source format is only compatible with R8 internal format!");
        passert (mipmapMode == VROMipmapMode::None);

        GL( glTexImage2D(target, 0, GL_R8, width, height, 0,
                         GL_RED, GL_UNSIGNED_BYTE, faceData->getData()) );
    }
    else {
        /*
         An unrecognised source format used to pabort(), which on web takes the
         emscripten main loop down with it: the canvas freezes on whatever had
         been drawn and the whole scene is lost over one texture. Warning and
         leaving the texture undefined costs that one surface instead.
         */
        pwarn("Unsupported texture format [%d]; leaving this face undefined",
              (int) format);
    }
}

void VROTextureSubstrateOpenGL::loadCompressedFace(GLenum target, GLenum compressedFormat,
                                                   VROMipmapMode mipmapMode,
                                                   std::shared_ptr<VROData> &faceData,
                                                   int width, int height,
                                                   const std::vector<uint32_t> &mipSizes) {
    const char *bytes = (const char *) faceData->getData();
    if (mipmapMode == VROMipmapMode::Pregenerated && !mipSizes.empty()) {
        // Levels are packed back to back. Each level is max(1, size >> level) on
        // each axis: a non-square texture keeps halving its long side after the
        // short side reaches 1 (256x64 ends 4x1, 2x1, 1x1), and GL rejects a 0.
        uint32_t offset = 0;
        for (size_t level = 0; level < mipSizes.size(); level++) {
            uint32_t mipSize = mipSizes[level];
            if ((uint64_t) offset + mipSize > (uint64_t) faceData->getDataLength()) {
                pwarn("Compressed texture data ends before mip level %d; truncating the chain",
                      (int) level);
                if (target == GL_TEXTURE_2D && level > 0) {
                    GL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint) level - 1) );
                }
                return;
            }
            GL( glCompressedTexImage2D(target, (GLint) level, compressedFormat,
                                       std::max(1, width >> level), std::max(1, height >> level), 0,
                                       mipSize, bytes + offset) );
            offset += mipSize;
        }
        // A chain shorter than log2(size) + 1 is only complete if GL is told
        // where it ends; otherwise mipmapped sampling reads an incomplete texture.
        if (target == GL_TEXTURE_2D) {
            GL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint) mipSizes.size() - 1) );
        }
    }
    else {
        // VROMipmapMode::None. The data may still hold mips we are not using; if
        // no mip sizes are provided, the whole buffer is level 0.
        GLsizei size = mipSizes.empty() ? (GLsizei) faceData->getDataLength() : (GLsizei) mipSizes.front();
        GL( glCompressedTexImage2D(target, 0, compressedFormat, width, height, 0, size, bytes) );
    }
}

GLuint VROTextureSubstrateOpenGL::getInternalFormat(VROTextureInternalFormat format, bool sRGB) {
    switch (format) {
        case VROTextureInternalFormat::RGBA8:
            return sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA;
        case VROTextureInternalFormat::RGBA4:
            return GL_RGBA4;
        case VROTextureInternalFormat::RGB565:
            return GL_RGB565;
        case VROTextureInternalFormat::R32F:
            return GL_R32F;
        case VROTextureInternalFormat::R8:
            return GL_R8;
        default:
            return GL_RGBA;
    }
}

GLenum VROTextureSubstrateOpenGL::convertWrapMode(VROWrapMode wrapMode) {
    switch (wrapMode) {
        case VROWrapMode::Clamp:
        case VROWrapMode::ClampToBorder:
            return GL_CLAMP_TO_EDGE;
        case VROWrapMode::Mirror:
            return GL_MIRRORED_REPEAT;
        default:
            return GL_REPEAT;
    }
}

GLenum VROTextureSubstrateOpenGL::convertMagFilter(VROFilterMode magFilter) {
  switch (magFilter) {
    case VROFilterMode::Nearest:
    case VROFilterMode::None:
      return GL_NEAREST;
    default:
      return GL_LINEAR;
  }
}

GLenum VROTextureSubstrateOpenGL::convertMinFilter(VROMipmapMode mipmapMode, VROFilterMode minFilter, VROFilterMode mipFilter) {
    if (minFilter == VROFilterMode::Nearest || minFilter == VROFilterMode::None) {
        if (mipmapMode != VROMipmapMode::None) {
            if (mipFilter == VROFilterMode::Linear) {
                return GL_NEAREST_MIPMAP_LINEAR;
            }
            else if (mipFilter == VROFilterMode::Nearest) {
                return GL_NEAREST_MIPMAP_NEAREST;
            }
            else {
                return GL_NEAREST;
            }
        }
        else {
            return GL_NEAREST;
        }
    }
    else if (minFilter == VROFilterMode::Linear) {
        if (mipmapMode != VROMipmapMode::None) {
            if (mipFilter == VROFilterMode::Linear) {
                return GL_LINEAR_MIPMAP_LINEAR;
            }
            else if (mipFilter == VROFilterMode::Nearest) {
                return GL_LINEAR_MIPMAP_NEAREST;
            }
            else {
                return GL_LINEAR;
            }
        }
        else {
            return GL_LINEAR;
        }
    }
    else {
        return GL_NEAREST;
    }
}
