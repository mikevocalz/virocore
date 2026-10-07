//
//  VROGlyphOpenGL.h
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

#ifndef VROGlyphOpenGL_h
#define VROGlyphOpenGL_h

#include "VROGlyph.h"
#include "VROOpenGL.h"

class VRODriver;
class VRODriverOpenGL;
class VROVectorizer;

namespace p2t {
    class Point;
}

class VROGlyphOpenGL : public VROGlyph {
    
public:
    
    VROGlyphOpenGL();
    virtual ~VROGlyphOpenGL();

    /*
     Scale applied to glyph metrics and quad sizes. Bitmap-strike fonts
     (e.g. NotoColorEmoji) report metrics in strike pixels rather than the
     requested point size; the owning typeface sets this to
     requestedSize / strikeSize so such glyphs lay out at text size while
     their (higher-resolution) atlas bitmap just maps onto a smaller quad.
     */
    void setPixelScale(float scale) {
        _pixelScale = scale;
    }

    bool loadMetrics(FT_Face face, uint32_t charCode, uint32_t variantSelector);
    bool loadBitmap(FT_Face face, uint32_t charCode, uint32_t variantSelector,
                    std::vector<std::shared_ptr<VROGlyphAtlas>> *atlases,
                    std::shared_ptr<VRODriver> driver);
    bool loadOutlineBitmap(FT_Library library, FT_Face face, uint32_t charCode, uint32_t variantSelector,
                           uint32_t outlineWidth,
                           std::vector<std::shared_ptr<VROGlyphAtlas>> *atlases,
                           std::shared_ptr<VRODriver> driver);
    bool loadVector(FT_Face face, uint32_t charCode, uint32_t variantSelector);
        
private:

    bool loadGlyph(FT_Face face, uint32_t charCode, uint32_t variantSelector);

    /*
     Fallback for embedded color bitmaps (CBDT/EBDT PNG strikes) that the
     bundled FreeType cannot rasterize because it was built without libpng.
     Parses CBLC/EBLC directly, extracts the glyph's PNG, and decodes it via
     stb_image into the alpha-only atlas format used everywhere else.
     */
    bool loadEmbeddedBitmap(FT_Face face, uint32_t charCode,
                            std::vector<std::shared_ptr<VROGlyphAtlas>> *atlases,
                            std::shared_ptr<VRODriver> driver);

    std::vector<p2t::Point *> triangulateContour(VROVectorizer &vectorizer, int c);
    
    float _pixelScale = 1.0f;
    
};

#endif /* VROGlyphOpenGL_h */
