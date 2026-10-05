//
//  VROGlyphOpenGL.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/24/16.
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

#include "VROGlyphOpenGL.h"
#include "VROLog.h"
#include "VROTexture.h"
#include "VROTextureSubstrateOpenGL.h"
#include "VROMath.h"
#include "VRODriverOpenGL.h"
#include "VRODefines.h"
#include "VROVectorizer.h"
#include "VROContour.h"
#include "VROGlyphAtlasOpenGL.h"
#include "stb_image.h"
#include "poly2tri/poly2tri.h"

#if VRO_PLATFORM_WASM
#include "ftstroke.h"
#include "tttables.h"
#else
#include "freetype/ftstroke.h"
#include "freetype/tttables.h"
#endif

#ifndef TTAG_CBLC
#define TTAG_CBLC FT_MAKE_TAG('C', 'B', 'L', 'C')
#endif
#ifndef TTAG_CBDT
#define TTAG_CBDT FT_MAKE_TAG('C', 'B', 'D', 'T')
#endif
#ifndef TTAG_EBLC
#define TTAG_EBLC FT_MAKE_TAG('E', 'B', 'L', 'C')
#endif
#ifndef TTAG_EBDT
#define TTAG_EBDT FT_MAKE_TAG('E', 'B', 'D', 'T')
#endif

static const int kBezierSteps = 4;
static const float kExtrusion = 1;

VROGlyphOpenGL::VROGlyphOpenGL() {
    
}

VROGlyphOpenGL::~VROGlyphOpenGL() {
    
}

bool VROGlyphOpenGL::loadGlyph(FT_Face face, uint32_t charCode, uint32_t variantSelector) {
    // Colour-capable fonts (e.g. NotoColorEmoji's CBDT bitmaps) only yield
    // their embedded bitmap when FT_LOAD_COLOR is set.
    const FT_Int32 loadFlags = FT_LOAD_DEFAULT | (FT_HAS_COLOR(face) ? FT_LOAD_COLOR : 0);
    if (variantSelector != 0) {
        FT_UInt glyphIndex = FT_Face_GetCharVariantIndex(face, charCode, variantSelector);
        if (glyphIndex == 0) {
            // Undefined character code, just attempt to load without the selector
            if (FT_Load_Char(face, charCode, loadFlags)) {
                pinfo("Failed to load glyph %d (dropped variant selector %d)", charCode, variantSelector);
                return false;
            }
        }
        else {
            if (FT_Load_Glyph(face, glyphIndex, loadFlags)) {
                pinfo("Failed to load glyph %d with variant selector %d", charCode, variantSelector);
                return false;
            }
        }
    }
    else if (FT_Load_Char(face, charCode, loadFlags)) {
        pinfo("Failed to load glyph %d", charCode);
        return false;
    }

    FT_GlyphSlot &glyph = face->glyph;

    /*
     Each advance unit is 1/64 of a pixel so divide by 64 to get advance in pixels.
     */
    _advance = static_cast<long>((glyph->advance.x / 64.0f) * _pixelScale);
    return true;
}

bool VROGlyphOpenGL::loadMetrics(FT_Face face, uint32_t charCode, uint32_t variantSelector) {
    return loadGlyph(face, charCode, variantSelector);
}

bool VROGlyphOpenGL::loadBitmap(FT_Face face, uint32_t charCode, uint32_t variantSelector,
                                std::vector<std::shared_ptr<VROGlyphAtlas>> *glyphAtlases,
                                std::shared_ptr<VRODriver> driver) {
    if (!loadGlyph(face, charCode, variantSelector)) {
        // Embedded color-bitmap strikes (CBDT PNG) cannot be rasterized by the
        // bundled FreeType build (no libpng); extract the strike directly.
        return loadEmbeddedBitmap(face, charCode, glyphAtlases, driver);
    }
    FT_GlyphSlot &glyph = face->glyph;

    if (glyphAtlases->empty()) {
        glyphAtlases->push_back(std::make_shared<VROGlyphAtlasOpenGL>(false));
    }
    std::shared_ptr<VROGlyphAtlas> atlas = glyphAtlases->back();

    FT_Render_Glyph(glyph, FT_RENDER_MODE_LIGHT);
    FT_Bitmap bitmap = glyph->bitmap;

    // Embedded colour bitmaps are BGRA; the atlas stores single-channel alpha
    // so reduce them to their alpha channel (a colour-emoji monochrome
    // silhouette — full colour needs a BGRA atlas, not a glyph change).
    std::vector<FT_Byte> bgraToAlpha;
    if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        bgraToAlpha.resize(static_cast<size_t>(bitmap.width) * bitmap.rows);
        for (unsigned int j = 0; j < bitmap.rows; j++) {
            for (unsigned int i = 0; i < bitmap.width; i++) {
                bgraToAlpha[i + j * bitmap.width] = bitmap.buffer[i * 4 + 3 + j * bitmap.pitch];
            }
        }
        bitmap.buffer = bgraToAlpha.data();
        bitmap.pitch = bitmap.width;
        bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        bitmap.num_grays = 256;
    }

    VROAtlasLocation location;
    if (atlas->glyphWillFit(bitmap, &location)) {
        atlas->write(bitmap, location, driver);
    } else {
        // Did not fit in the atlas, create a new atlas
        glyphAtlases->push_back(std::make_shared<VROGlyphAtlasOpenGL>(false));
        atlas = glyphAtlases->back();

        if (atlas->glyphWillFit(bitmap, &location)) {
            atlas->write(bitmap, location, driver);
        } else {
            pinfo("Failed to render glyph for char code %d", charCode);
            return false;
        }
    }

    VROGlyphBitmap vBitmap;
    vBitmap.atlas = atlas;
    vBitmap.bearing = VROVector3f(glyph->bitmap_left * _pixelScale, glyph->bitmap_top * _pixelScale);
    vBitmap.size = VROVector3f(bitmap.width * _pixelScale, bitmap.rows * _pixelScale);
    vBitmap.minU = ((float) location.minU) / (float) atlas->getSize();
    vBitmap.maxU = ((float) location.maxU) / (float) atlas->getSize();
    vBitmap.minV = ((float) location.minV) / (float) atlas->getSize();
    vBitmap.maxV = ((float) location.maxV) / (float) atlas->getSize();

    _bitmaps[0] = vBitmap;
    return true;
}

static inline uint16_t readBE16(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
static inline uint32_t readBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

static bool loadSfntTable(FT_Face face, FT_ULong tag, std::vector<FT_Byte> &out) {
    FT_ULong len = 0;
    if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &len) != 0 || len == 0) {
        return false;
    }
    out.resize(len);
    return FT_Load_Sfnt_Table(face, tag, 0, out.data(), &len) == 0;
}

bool VROGlyphOpenGL::loadEmbeddedBitmap(FT_Face face, uint32_t charCode,
                                        std::vector<std::shared_ptr<VROGlyphAtlas>> *glyphAtlases,
                                        std::shared_ptr<VRODriver> driver) {
    FT_UInt glyphId = FT_Get_Char_Index(face, charCode);
    if (glyphId == 0) {
        return false;
    }

    std::vector<FT_Byte> cblc, cbdt;
    if (!loadSfntTable(face, TTAG_CBLC, cblc) && !loadSfntTable(face, TTAG_EBLC, cblc)) {
        return false;
    }
    if (!loadSfntTable(face, TTAG_CBDT, cbdt) && !loadSfntTable(face, TTAG_EBDT, cbdt)) {
        return false;
    }
    if (cblc.size() < 8) {
        return false;
    }

    // CBLC: u32 version, u32 numSizes, then 48-byte bitmapSize records.
    const uint32_t numSizes = readBE32(cblc.data() + 4);
    const int wantedPpem = face->size ? face->size->metrics.y_ppem : 0;
    const uint8_t *sizeRecord = nullptr;
    int bestPpemDiff = INT32_MAX;
    for (uint32_t i = 0; i < numSizes; i++) {
        const uint8_t *rec = cblc.data() + 8 + i * 48;
        if (cblc.data() + cblc.size() < rec + 48) {
            break;
        }
        const uint16_t startGlyph = readBE16(rec + 40);
        const uint16_t endGlyph = readBE16(rec + 42);
        if (glyphId < startGlyph || glyphId > endGlyph) {
            continue;
        }
        int diff = wantedPpem > 0 ? std::abs(rec[45] - wantedPpem) : 0;
        if (diff < bestPpemDiff) {
            bestPpemDiff = diff;
            sizeRecord = rec;
        }
    }
    if (!sizeRecord) {
        return false;
    }

    const uint32_t idxArrayOff = readBE32(sizeRecord);
    const uint32_t numIdxSubTables = readBE32(sizeRecord + 8);

    // Each indexSubTableArray entry: firstGlyph u16, lastGlyph u16,
    // additionalOffsetToIndexSubtable u32 (relative to the array start).
    const uint8_t *subTable = nullptr;
    uint16_t firstGlyph = 0, lastGlyph = 0;
    for (uint32_t i = 0; i < numIdxSubTables; i++) {
        const uint8_t *entry = cblc.data() + idxArrayOff + i * 8;
        if (cblc.data() + cblc.size() < entry + 8) {
            break;
        }
        firstGlyph = readBE16(entry);
        lastGlyph = readBE16(entry + 2);
        if (glyphId >= firstGlyph && glyphId <= lastGlyph) {
            const uint32_t addOff = readBE32(entry + 4);
            if (idxArrayOff + addOff + 8 <= cblc.size()) {
                subTable = cblc.data() + idxArrayOff + addOff;
            }
            break;
        }
    }
    if (!subTable) {
        return false;
    }

    const uint16_t indexFormat = readBE16(subTable);
    const uint16_t imageFormat = readBE16(subTable + 2);
    const uint32_t imageDataOffset = readBE32(subTable + 4);

    // Resolve the glyph's image-data offset/length within CBDT.
    uint32_t imageOff = 0, imageLen = 0;
    const uint32_t glyphCount = static_cast<uint32_t>(lastGlyph) - firstGlyph + 1;
    if (indexFormat == 1) {
        // u32 offsetArray[glyphCount + 1] relative to imageDataOffset.
        const uint8_t *arr = subTable + 8;
        const uint32_t idx = glyphId - firstGlyph;
        if (arr + (idx + 2) * 4 > cblc.data() + cblc.size()) {
            return false;
        }
        imageOff = readBE32(arr + idx * 4);
        imageLen = readBE32(arr + (idx + 1) * 4) - imageOff;
    } else if (indexFormat == 3) {
        // Array of {glyphId u16, offset u32} pairs relative to imageDataOffset.
        const uint8_t *pairs = subTable + 8;
        for (uint32_t i = 0; i < glyphCount; i++) {
            const uint8_t *pair = pairs + i * 6;
            if (pair + 6 > cblc.data() + cblc.size()) {
                return false;
            }
            if (readBE16(pair) == glyphId) {
                imageOff = readBE32(pair + 2);
                if (i + 1 < glyphCount) {
                    imageLen = readBE32(pair + 8) - imageOff;
                } else {
                    imageLen = static_cast<uint32_t>(cbdt.size()) - imageDataOffset - imageOff;
                }
                break;
            }
        }
        if (imageLen == 0) {
            return false;
        }
    } else {
        return false;
    }

    const uint8_t *image = cbdt.data() + imageDataOffset + imageOff;
    if (imageOff == 0 || imageDataOffset + imageOff + imageLen > cbdt.size()) {
        return false;
    }

    // imageFormat 17: smallGlyphMetrics(5B) + dataLen u32 + PNG;
    // 18: bigGlyphMetrics(8B) + dataLen u32 + PNG; 19: raw image data.
    int bearingX = 0, bearingY = 0, advance = 0;
    const uint8_t *png = image;
    uint32_t pngLen = imageLen;
    if (imageFormat == 17 && imageLen >= 9) {
        bearingX = static_cast<int8_t>(image[2]);
        bearingY = static_cast<int8_t>(image[3]);
        advance = image[4];
        pngLen = readBE32(image + 5);
        png += 9;
        if (pngLen > imageLen - 9) {
            return false;
        }
    } else if (imageFormat == 18 && imageLen >= 12) {
        bearingX = static_cast<int8_t>(image[2]);
        bearingY = static_cast<int8_t>(image[3]);
        advance = image[4];
        pngLen = readBE32(image + 8);
        png += 12;
        if (pngLen > imageLen - 12) {
            return false;
        }
    } else if (imageFormat != 19) {
        return false;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc *rgba = stbi_load_from_memory(png, static_cast<int>(pngLen), &w, &h, &comp, 4);
    if (!rgba) {
        return false;
    }

    // Alpha-only silhouette, same atlas format as other glyphs.
    std::vector<FT_Byte> alpha(static_cast<size_t>(w) * h);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            alpha[i + j * w] = rgba[(i + j * w) * 4 + 3];
        }
    }
    stbi_image_free(rgba);

    if (advance == 0) {
        advance = w;
    }
    _advance = static_cast<long>(advance * _pixelScale);

    FT_Bitmap bitmap;
    bitmap.width = static_cast<unsigned int>(w);
    bitmap.rows = static_cast<unsigned int>(h);
    bitmap.pitch = w;
    bitmap.buffer = alpha.data();
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitmap.num_grays = 256;

    if (glyphAtlases->empty()) {
        glyphAtlases->push_back(std::make_shared<VROGlyphAtlasOpenGL>(false));
    }
    std::shared_ptr<VROGlyphAtlas> atlas = glyphAtlases->back();

    VROAtlasLocation location;
    if (atlas->glyphWillFit(bitmap, &location)) {
        atlas->write(bitmap, location, driver);
    } else {
        glyphAtlases->push_back(std::make_shared<VROGlyphAtlasOpenGL>(false));
        atlas = glyphAtlases->back();
        if (atlas->glyphWillFit(bitmap, &location)) {
            atlas->write(bitmap, location, driver);
        } else {
            pinfo("Failed to render glyph for char code %d", charCode);
            return false;
        }
    }

    VROGlyphBitmap vBitmap;
    vBitmap.atlas = atlas;
    vBitmap.bearing = VROVector3f(bearingX * _pixelScale, bearingY * _pixelScale);
    vBitmap.size = VROVector3f(bitmap.width * _pixelScale, bitmap.rows * _pixelScale);
    vBitmap.minU = ((float) location.minU) / (float) atlas->getSize();
    vBitmap.maxU = ((float) location.maxU) / (float) atlas->getSize();
    vBitmap.minV = ((float) location.minV) / (float) atlas->getSize();
    vBitmap.maxV = ((float) location.maxV) / (float) atlas->getSize();

    _bitmaps[0] = vBitmap;
    return true;
}

bool VROGlyphOpenGL::loadOutlineBitmap(FT_Library library, FT_Face face, uint32_t charCode, uint32_t variantSelector,
                                       uint32_t outlineWidth,
                                       std::vector<std::shared_ptr<VROGlyphAtlas>> *glyphAtlases,
                                       std::shared_ptr<VRODriver> driver) {
    if (!loadGlyph(face, charCode, variantSelector)) {
        return false;
    }
    
    FT_GlyphSlot &glyphSlot = face->glyph;
    if (glyphAtlases->empty()) {
        glyphAtlases->push_back(std::make_shared<VROGlyphAtlasOpenGL>(true));
    }
    std::shared_ptr<VROGlyphAtlas> atlas = glyphAtlases->back();
    
    FT_Stroker stroker;
    FT_Stroker_New(library, &stroker);
    FT_Stroker_Set(stroker, outlineWidth * 64, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);

    FT_Glyph glyph;
    FT_Get_Glyph(glyphSlot, &glyph);
    FT_Glyph_StrokeBorder(&glyph, stroker, false, true);
    FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, true);
    FT_BitmapGlyph bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph);
    FT_Bitmap &bitmap = bitmapGlyph->bitmap;
    
    VROAtlasLocation location;
    if (atlas->glyphWillFit(bitmap, &location)) {
        atlas->write(bitmap, location, driver);
    } else {
        // Did not fit in the atlas, create a new atlas
        glyphAtlases->push_back(std::make_shared<VROGlyphAtlasOpenGL>(true));
        atlas = glyphAtlases->back();
        
        if (atlas->glyphWillFit(bitmap, &location)) {
            atlas->write(bitmap, location, driver);
        } else {
            pinfo("Failed to render glyph for char code %d", charCode);
            return false;
        }
    }
    
    VROGlyphBitmap vBitmap;
    vBitmap.atlas = atlas;
    vBitmap.bearing = VROVector3f(glyphSlot->bitmap_left * _pixelScale, glyphSlot->bitmap_top * _pixelScale);
    vBitmap.size = VROVector3f(bitmap.width * _pixelScale, bitmap.rows * _pixelScale);
    vBitmap.minU = ((float) location.minU) / (float) atlas->getSize();
    vBitmap.maxU = ((float) location.maxU) / (float) atlas->getSize();
    vBitmap.minV = ((float) location.minV) / (float) atlas->getSize();
    vBitmap.maxV = ((float) location.maxV) / (float) atlas->getSize();

    _bitmaps[(int) outlineWidth] = vBitmap;
    return true;
}

bool VROGlyphOpenGL::loadVector(FT_Face face, uint32_t charCode, uint32_t variantSelector) {
    if (!loadGlyph(face, charCode, variantSelector)) {
        return false;
    }
    
    FT_GlyphSlot &glyph = face->glyph;
    if (glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        pwarn("Outline glyph format required to vectorize, aborting 3D font building");
        return false;
    }
    
    VROVectorizer vectorizer(glyph, kBezierSteps);
    
    /*
     Contour the sides.
     */
    for (size_t ci = 0; ci < vectorizer.getContourCount(); ++ci) {
        const VROContour *contour = vectorizer.getContour(ci);
        for (size_t p = 0; p < contour->getPointCount() - 1; ++p) {
            const VROVector3f d1 = contour->getPoint(p);
            const VROVector3f d2 = contour->getPoint(p + 1);
            
            VROVector3f a, b, c;
            a.x = (d1.x / 64.0f);
            a.y = d1.y / 64.0f;
            a.z = 0.0f;
            b.x = (d2.x / 64.0f);
            b.y = d2.y / 64.0f;
            b.z = 0.0f;
            c.x = (d1.x / 64.0f);
            c.y = d1.y / 64.0f;
            c.z = kExtrusion;
            
            VROGlyphTriangle t1(a, b, c, VROGlyphTriangleType::Side);
            _triangles.push_back(t1);
            
            a.x = (d1.x / 64.0f);
            a.y = d1.y / 64.0f;
            a.z = kExtrusion;
            b.x = (d2.x / 64.0f);
            b.y = d2.y / 64.0f;
            b.z = kExtrusion;
            c.x = (d2.x / 64.0f);
            c.y = d2.y / 64.0f;
            c.z = 0.0f;
            
            VROGlyphTriangle t2(a, b, c, VROGlyphTriangleType::Side);
            _triangles.push_back(t2);
        }
        
        /*
         Add the last triangle closing the contour of the sides.
         */
        const VROVector3f d1 = contour->getPoint(contour->getPointCount() - 1);
        const VROVector3f d2 = contour->getPoint(0);
        VROVector3f a, b, c;
        a.x = (d1.x / 64.0f);
        a.y = d1.y / 64.0f;
        a.z = 0.0f;
        b.x = (d2.x / 64.0f);
        b.y = d2.y / 64.0f;
        b.z = 0.0f;
        c.x = (d1.x / 64.0f);
        c.y = d1.y / 64.0f;
        c.z = kExtrusion;
        
        VROGlyphTriangle t1(a, b, c, VROGlyphTriangleType::Side);
        _triangles.push_back(t1);
        
        a.x = (d1.x / 64.0f);
        a.y = d1.y / 64.0f;
        a.z = kExtrusion;
        b.x = (d2.x / 64.0f);
        b.y = d2.y / 64.0f;
        b.z = kExtrusion;
        c.x = (d2.x / 64.0f);
        c.y = d2.y / 64.0f;
        c.z = 0.0f;
        
        VROGlyphTriangle t2(a, b, c, VROGlyphTriangleType::Side);
        _triangles.push_back(t2);
        
        if (contour->getDirection()) {
            try {
                std::vector<p2t::Point *> polyline = triangulateContour(vectorizer, (int) ci);
                p2t::CDT *cdt = new p2t::CDT(polyline);

                for (size_t cm = 0; cm < vectorizer.getContourCount(); ++cm) {
                    const VROContour *sm = vectorizer.getContour(cm);
                    if (ci != cm && !sm->getDirection() && sm->isInside(contour)) {
                        std::vector<p2t::Point *> pl = triangulateContour(vectorizer, (int) cm);
                        cdt->AddHole(pl);
                    }
                }

                cdt->Triangulate();
                std::vector<p2t::Triangle *> ts = cdt->GetTriangles();
                for (int i = 0; i < ts.size(); i++) {
                    p2t::Triangle *ot = ts[i];

                    VROVector3f a, b, c;
                    a.x = ot->GetPoint(0)->x;
                    a.y = ot->GetPoint(0)->y;
                    a.z = 0.0f;
                    b.x = ot->GetPoint(1)->x;
                    b.y = ot->GetPoint(1)->y;
                    b.z = 0.0f;
                    c.x = ot->GetPoint(2)->x;
                    c.y = ot->GetPoint(2)->y;
                    c.z = 0.0f;

                    VROGlyphTriangle t1(a, b, c, VROGlyphTriangleType::Back);
                    _triangles.push_back(t1);

                    a.x = ot->GetPoint(0)->x;
                    a.y = ot->GetPoint(0)->y;
                    a.z = kExtrusion;
                    b.x = ot->GetPoint(1)->x;
                    b.y = ot->GetPoint(1)->y;
                    b.z = kExtrusion;
                    c.x = ot->GetPoint(2)->x;
                    c.y = ot->GetPoint(2)->y;
                    c.z = kExtrusion;

                    VROGlyphTriangle t2(a, b, c, VROGlyphTriangleType::Front);
                    _triangles.push_back(t2);
                }
                delete (cdt);
            } catch (const std::exception &e) {
                pwarn("Exception while triangulating text; text may be malformed");
            }
        }
    }
    return true;
}

std::vector<p2t::Point *> VROGlyphOpenGL::triangulateContour(VROVectorizer &vectorizer, int c) {
    std::vector<p2t::Point*> polyline;
    const VROContour *contour = vectorizer.getContour(c);
    for(size_t p = 0; p < contour->getPointCount(); ++p) {
        VROVector3f d = contour->getPoint(p);
        polyline.push_back(new p2t::Point((d.x / 64.0f), d.y / 64.0f));
    }
    return polyline;
}
