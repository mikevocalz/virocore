//
//  VROGeometrySubstrateMetal.h
//  ViroRenderer
//
//  Created by Raj Advani on 11/18/15.
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

#ifndef VROGeometrySubstrateMetal_h
#define VROGeometrySubstrateMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include "VROGeometrySubstrate.h"
#include "VROGeometryElement.h"
#include "VROMatrix4f.h"
#include <Metal/Metal.h>
#include <vector>
#include <map>
#include <memory>

class VROGeometry;
class VROMaterial;
class VROGeometrySource;
class VROGeometryElement;
class VRORenderContext;
class VRODriver;
class VRODriverMetal;
class VROMaterialSubstrateMetal;

/*
 A Metal vertex attribute pulled from a VROGeometrySource: the attribute
 index is the shader attribute slot (VROGeometryUtilParseAttributeIndex),
 the offset/stride describe the source's layout within its shared buffer.
 */
struct VROVertexAttributeMetal {
    int attributeIndex;
    int offset;
    MTLVertexFormat format;
};

/*
 All VROGeometrySources that share one underlying data buffer become a
 single MTLBuffer plus a list of attributes; the buffer is bound to the
 render encoder at bufferIndex.
 */
struct VROVertexDescriptorMetal {
    id <MTLBuffer> buffer;
    int bufferIndex;
    int stride;
    std::vector<VROVertexAttributeMetal> attributes;
};

struct VROGeometryElementMetal {
    id <MTLBuffer> buffer;
    MTLPrimitiveType primitiveType;
    int indexCount;
    MTLIndexType indexType;
    int indexBufferOffset;
};

/*
 Metal representation of a VROGeometry (minimal milestone implementation).

 Vertex/index data is uploaded once into MTLBuffers. At render time we bake
 an MTLRenderPipelineState from (a) the material's shader functions, (b) the
 geometry's vertex layout, (c) the driver's current blend mode and pixel
 formats; and an MTLDepthStencilState from the driver's depth flags. Both
 are cached per element.

 Not yet supported (logged, not silently skipped): skinning/bones, morph
 targets, instancing, vertex buffers owned by VROVertexBuffer objects,
 texture binding.
 */
class VROGeometrySubstrateMetal : public VROGeometrySubstrate {

public:

    VROGeometrySubstrateMetal(const VROGeometry &geometry,
                              VRODriverMetal &driver);
    virtual ~VROGeometrySubstrateMetal();

    void update(const VROGeometry &geometry,
                std::shared_ptr<VRODriver> &driver) override;

    void render(const VROGeometry &geometry,
                int elementIndex,
                VROMatrix4f transform,
                VROMatrix4f normalMatrix,
                float opacity,
                const std::shared_ptr<VROMaterial> &material,
                const VRORenderContext &context,
                std::shared_ptr<VRODriver> &driver) override;

    void renderSilhouette(const VROGeometry &geometry,
                          VROMatrix4f transform,
                          std::shared_ptr<VROMaterial> &material,
                          const VRORenderContext &context,
                          std::shared_ptr<VRODriver> &driver) override;

    void renderSilhouetteTextured(const VROGeometry &geometry,
                                  int element,
                                  VROMatrix4f transform,
                                  std::shared_ptr<VROMaterial> &material,
                                  const VRORenderContext &context,
                                  std::shared_ptr<VRODriver> &driver) override;

private:

    std::vector<VROVertexDescriptorMetal> _vertexDescriptors;
    std::vector<VROGeometryElementMetal> _elements;
    std::map<int, std::vector<int>> _elementToDescriptorsMap;

    // Pipeline and depth-stencil state caches, keyed by a small composite
    // (element index, shader variant, blend mode, depth flags).
    std::map<uint32_t, id <MTLRenderPipelineState>> _pipelineStates;
    std::map<uint32_t, id <MTLDepthStencilState>> _depthStates;

    void readGeometryElements(id <MTLDevice> device,
                              const std::vector<std::shared_ptr<VROGeometryElement>> &elements);
    void readGeometrySources(id <MTLDevice> device,
                             const std::vector<std::shared_ptr<VROGeometrySource>> &sources);

    // True if the descriptors for the given element include both Normal and
    // Texcoord attributes (selects the 'full' vertex shader variant).
    bool hasFullVertexLayout(int elementIndex) const;

    std::vector<const VROVertexDescriptorMetal *> descriptorsForElement(int elementIndex) const;

    id <MTLRenderPipelineState> getPipelineState(int elementIndex,
                                                 VROMaterialSubstrateMetal *material,
                                                 VRODriverMetal &driver);
    id <MTLDepthStencilState> getDepthStencilState(VRODriverMetal &driver);

    void renderElement(const VROGeometry &geometry,
                       int elementIndex,
                       VROMatrix4f transform,
                       VROMatrix4f normalMatrix,
                       float opacity,
                       std::shared_ptr<VROMaterial> &material,
                       const VRORenderContext &context,
                       std::shared_ptr<VRODriver> &driver);

    MTLVertexFormat parseVertexFormat(std::shared_ptr<VROGeometrySource> &source);
    MTLPrimitiveType parsePrimitiveType(VROGeometryPrimitiveType primitive);

    bool _warnedVBO;
    bool _warnedEncoder;
    bool _warnedSkinner;
    bool _warnedSources;

};

#endif // VRO_METAL
#endif /* VROGeometrySubstrateMetal_h */
