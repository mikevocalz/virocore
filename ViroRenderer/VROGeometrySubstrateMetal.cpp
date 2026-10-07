//
//  VROGeometrySubstrateMetal.cpp
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

#include "VROGeometrySubstrateMetal.h"
#if VRO_METAL

#include "VROGeometry.h"
#include "VROGeometrySource.h"
#include "VROGeometryElement.h"
#include "VROGeometryUtil.h"
#include "VRODriverMetal.h"
#include "VROMaterialSubstrateMetal.h"
#include "VROMaterial.h"
#include "VROVertexBuffer.h"
#include "VROData.h"
#include "VROLog.h"
#include "VRORenderContext.h"
#include "VROCamera.h"
#include "VROAllocationTracker.h"
#include <map>

VROGeometrySubstrateMetal::VROGeometrySubstrateMetal(const VROGeometry &geometry,
                                                     VRODriverMetal &driver) :
    _warnedVBO(false),
    _warnedEncoder(false),
    _warnedSkinner(false),
    _warnedSources(false) {
    id <MTLDevice> device = driver.getDevice();
    readGeometryElements(device, geometry.getGeometryElements());
    readGeometrySources(device, geometry.getGeometrySources());
    if (geometry.getSkinner() && !_warnedSkinner) {
        pwarn("VROGeometrySubstrateMetal: skinned geometry — bones ignored (unsupported)");
        _warnedSkinner = true;
    }
}

VROGeometrySubstrateMetal::~VROGeometrySubstrateMetal() {
    for (VROGeometryElementMetal &element : _elements) {
        element.buffer = nil;
    }
    for (VROVertexDescriptorMetal &vd : _vertexDescriptors) {
        vd.buffer = nil;
        ALLOCATION_TRACKER_SUB(VBO, 1);
    }
    _vertexDescriptors.clear();
    _elements.clear();
    _elementToDescriptorsMap.clear();
    _pipelineStates.clear();
    _depthStates.clear();
}

void VROGeometrySubstrateMetal::readGeometryElements(id <MTLDevice> device,
                                                   const std::vector<std::shared_ptr<VROGeometryElement>> &elements) {
    for (std::shared_ptr<VROGeometryElement> element : elements) {
        if (element->getData() == nullptr) {
            continue;
        }
        VROGeometryElementMetal elementMetal;

        int indexCount = VROGeometryUtilGetIndicesCount(element->getPrimitiveCount(),
                                                      element->getPrimitiveType());
        elementMetal.buffer = [device newBufferWithBytes:element->getData()->getData()
                                                  length:indexCount * element->getBytesPerIndex()
                                                 options:MTLResourceStorageModeShared];
        elementMetal.primitiveType = parsePrimitiveType(element->getPrimitiveType());
        elementMetal.indexCount = indexCount;
        elementMetal.indexType = (element->getBytesPerIndex() == 2) ? MTLIndexTypeUInt16
                                                                    : MTLIndexTypeUInt32;
        elementMetal.indexBufferOffset = 0;
        _elements.push_back(elementMetal);
    }
}

void VROGeometrySubstrateMetal::readGeometrySources(id <MTLDevice> device,
                                                  const std::vector<std::shared_ptr<VROGeometrySource>> &sources) {
    std::map<std::shared_ptr<VROData>, std::vector<std::shared_ptr<VROGeometrySource>>> dataMap;

    // Sort the sources into groups defined by the data buffer they're using.
    // Sources backed by a VROVertexBuffer use its underlying VROData (the
    // Metal vertex buffer type keeps the CPU copy; hydrate() is a no-op).
    for (std::shared_ptr<VROGeometrySource> source : sources) {
        std::shared_ptr<VROData> data;
        if (source->getVertexBuffer()) {
            data = source->getVertexBuffer()->getData();
            if (!_warnedVBO) {
                pwarn("VROGeometrySubstrateMetal: VROVertexBuffer-backed source — uploading a private copy (dynamic updates unsupported)");
                _warnedVBO = true;
            }
        }
        else {
            data = source->getData();
        }
        if (data && data->getDataLength() > 0) {
            dataMap[data].push_back(source);
        }
        else if (!_warnedSources) {
            pwarn("VROGeometrySubstrateMetal: geometry source with no data — skipped");
            _warnedSources = true;
        }
    }

    int bufferIndex = 0;
    for (auto &kv : dataMap) {
        if (bufferIndex >= kVROMetalViewUniformsIndex) {
            pwarn("VROGeometrySubstrateMetal: too many vertex buffers (%d) — dropping group", bufferIndex);
            break;
        }
        std::shared_ptr<VROData> data = kv.first;
        std::vector<std::shared_ptr<VROGeometrySource>> group = kv.second;

        VROVertexDescriptorMetal vd;
        vd.buffer = [device newBufferWithBytes:data->getData()
                                        length:data->getDataLength()
                                       options:MTLResourceStorageModeShared];
        vd.bufferIndex = bufferIndex++;
        vd.stride = group[0]->getDataStride();
        ALLOCATION_TRACKER_ADD(VBO, 1);

        for (std::shared_ptr<VROGeometrySource> &source : group) {
            passert (source->getData() == data || source->getVertexBuffer() != nullptr);
            VROVertexAttributeMetal attr;
            attr.attributeIndex = VROGeometryUtilParseAttributeIndex(source->getSemantic());
            attr.offset = source->getDataOffset();
            attr.format = parseVertexFormat(source);
            passert (source->getDataStride() == vd.stride);
            vd.attributes.push_back(attr);

            int elementIndex = source->getGeometryElementIndex();
            if (elementIndex != -1) {
                _elementToDescriptorsMap[elementIndex].push_back((int)_vertexDescriptors.size());
            }
        }
        _vertexDescriptors.push_back(vd);
    }
}

MTLVertexFormat VROGeometrySubstrateMetal::parseVertexFormat(std::shared_ptr<VROGeometrySource> &source) {
    if (!source->isFloatComponents()) {
        switch (source->getBytesPerComponent()) {
            case 1:
                switch (source->getComponentsPerVertex()) {
                    case 1: return MTLVertexFormatUChar;
                    case 2: return MTLVertexFormatUChar2;
                    case 3: return MTLVertexFormatUChar3;
                    case 4: return MTLVertexFormatUChar4;
                }
                break;
            case 2:
                switch (source->getComponentsPerVertex()) {
                    case 1: return MTLVertexFormatUShort;
                    case 2: return MTLVertexFormatUShort2;
                    case 3: return MTLVertexFormatUShort3;
                    case 4: return MTLVertexFormatUShort4;
                }
                break;
            case 4:
                switch (source->getComponentsPerVertex()) {
                    case 1: return MTLVertexFormatUInt;
                    case 2: return MTLVertexFormatUInt2;
                    case 3: return MTLVertexFormatUInt3;
                    case 4: return MTLVertexFormatUInt4;
                }
                break;
        }
        pabort("Unsupported vertex format");
    }
    switch (source->getBytesPerComponent()) {
        case 2:
            switch (source->getComponentsPerVertex()) {
                case 1: return MTLVertexFormatHalf;
                case 2: return MTLVertexFormatHalf2;
                case 3: return MTLVertexFormatHalf3;
                case 4: return MTLVertexFormatHalf4;
            }
            break;
        case 4:
            switch (source->getComponentsPerVertex()) {
                case 1: return MTLVertexFormatFloat;
                case 2: return MTLVertexFormatFloat2;
                case 3: return MTLVertexFormatFloat3;
                case 4: return MTLVertexFormatFloat4;
            }
            break;
    }
    pabort("Unsupported vertex format");
    return MTLVertexFormatFloat3;
}

MTLPrimitiveType VROGeometrySubstrateMetal::parsePrimitiveType(VROGeometryPrimitiveType primitive) {
    switch (primitive) {
        case VROGeometryPrimitiveType::Triangle:      return MTLPrimitiveTypeTriangle;
        case VROGeometryPrimitiveType::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
        case VROGeometryPrimitiveType::Line:          return MTLPrimitiveTypeLine;
        case VROGeometryPrimitiveType::Point:         return MTLPrimitiveTypePoint;
        default:                                      return MTLPrimitiveTypeTriangle;
    }
}

void VROGeometrySubstrateMetal::update(const VROGeometry &geometry,
                                       std::shared_ptr<VRODriver> &driver) {
    // No per-frame GPU work: skinning/morph/instancing are unsupported.
}

std::vector<const VROVertexDescriptorMetal *> VROGeometrySubstrateMetal::descriptorsForElement(int elementIndex) const {
    std::vector<const VROVertexDescriptorMetal *> result;
    auto it = _elementToDescriptorsMap.find(elementIndex);
    if (it != _elementToDescriptorsMap.end() && !it->second.empty()) {
        for (int idx : it->second) {
            result.push_back(&_vertexDescriptors[idx]);
        }
    }
    else {
        for (const VROVertexDescriptorMetal &vd : _vertexDescriptors) {
            result.push_back(&vd);
        }
    }
    return result;
}

bool VROGeometrySubstrateMetal::hasFullVertexLayout(int elementIndex) const {
    bool hasNormal = false, hasTexcoord = false;
    for (const VROVertexDescriptorMetal *vd : descriptorsForElement(elementIndex)) {
        for (const VROVertexAttributeMetal &attr : vd->attributes) {
            if (attr.attributeIndex ==
                VROGeometryUtilParseAttributeIndex(VROGeometrySourceSemantic::Normal)) {
                hasNormal = true;
            }
            if (attr.attributeIndex ==
                VROGeometryUtilParseAttributeIndex(VROGeometrySourceSemantic::Texcoord)) {
                hasTexcoord = true;
            }
        }
    }
    return hasNormal && hasTexcoord;
}

id <MTLRenderPipelineState> VROGeometrySubstrateMetal::getPipelineState(int elementIndex,
                                                                        VROMaterialSubstrateMetal *material,
                                                                        VRODriverMetal &driver) {
    VROMetalVertexVariant variant = hasFullVertexLayout(elementIndex)
        ? VROMetalVertexVariant::Full : VROMetalVertexVariant::PositionOnly;
    VROBlendMode blend = driver.getBlendMode();

    uint32_t key = ((uint32_t)elementIndex & 0xF) << 8
                 | ((uint32_t)variant & 0xF) << 4
                 | ((uint32_t)blend & 0xF);
    auto it = _pipelineStates.find(key);
    if (it != _pipelineStates.end()) {
        return it->second;
    }

    MTLVertexDescriptor *vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    for (const VROVertexDescriptorMetal *vd : descriptorsForElement(elementIndex)) {
        vertexDescriptor.layouts[vd->bufferIndex].stride = vd->stride;
        vertexDescriptor.layouts[vd->bufferIndex].stepRate = 1;
        vertexDescriptor.layouts[vd->bufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
        for (const VROVertexAttributeMetal &attr : vd->attributes) {
            vertexDescriptor.attributes[attr.attributeIndex].format = attr.format;
            vertexDescriptor.attributes[attr.attributeIndex].offset = attr.offset;
            vertexDescriptor.attributes[attr.attributeIndex].bufferIndex = vd->bufferIndex;
        }
    }

    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexDescriptor = vertexDescriptor;
    pd.vertexFunction = material->getVertexProgram(variant);
    pd.fragmentFunction = material->getFragmentProgram();
    pd.colorAttachments[0].pixelFormat = driver.getColorPixelFormat();
    pd.depthAttachmentPixelFormat = driver.getDepthPixelFormat();
    if (driver.getStencilPixelFormat() != MTLPixelFormatInvalid) {
        pd.stencilAttachmentPixelFormat = driver.getStencilPixelFormat();
    }
    pd.sampleCount = driver.getSampleCount();

    // Bake the current blend mode into the pipeline (Metal has no global blend toggle).
    MTLRenderPipelineColorAttachmentDescriptor *color = pd.colorAttachments[0];
    switch (blend) {
        case VROBlendMode::None:
            color.blendingEnabled = NO;
            break;
        case VROBlendMode::Alpha:
            color.blendingEnabled = YES;
            color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            color.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
            color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            break;
        case VROBlendMode::PremultiplyAlpha:
            color.blendingEnabled = YES;
            color.sourceRGBBlendFactor = MTLBlendFactorOne;
            color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            color.sourceAlphaBlendFactor = MTLBlendFactorOne;
            color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            break;
        case VROBlendMode::Add:
        case VROBlendMode::Screen:
            color.blendingEnabled = YES;
            color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            color.destinationRGBBlendFactor = MTLBlendFactorOne;
            color.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
            color.destinationAlphaBlendFactor = MTLBlendFactorOne;
            break;
        default:
            pwarn("VROGeometrySubstrateMetal: blend mode %d approximated as Alpha", (int)blend);
            color.blendingEnabled = YES;
            color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            break;
    }

    NSError *error = nil;
    id <MTLRenderPipelineState> pipeline =
        [driver.getDevice() newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!pipeline) {
        pwarn("VROGeometrySubstrateMetal: pipeline creation failed: %s",
              error ? [[error localizedDescription] UTF8String] : "?");
        return nil;
    }
    _pipelineStates[key] = pipeline;
    return pipeline;
}

id <MTLDepthStencilState> VROGeometrySubstrateMetal::getDepthStencilState(VRODriverMetal &driver) {
    uint32_t key = (driver.isDepthReadingEnabled() ? 1 : 0)
                 | (driver.isDepthWritingEnabled() ? 2 : 0);
    auto it = _depthStates.find(key);
    if (it != _depthStates.end()) {
        return it->second;
    }

    MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = driver.isDepthReadingEnabled() ? MTLCompareFunctionLessEqual
                                                             : MTLCompareFunctionAlways;
    dd.depthWriteEnabled = driver.isDepthWritingEnabled();
    id <MTLDepthStencilState> state = [driver.getDevice() newDepthStencilStateWithDescriptor:dd];
    _depthStates[key] = state;
    return state;
}

void VROGeometrySubstrateMetal::render(const VROGeometry &geometry,
                                       int elementIndex,
                                       VROMatrix4f transform,
                                       VROMatrix4f normalMatrix,
                                       float opacity,
                                       const std::shared_ptr<VROMaterial> &material,
                                       const VRORenderContext &context,
                                       std::shared_ptr<VRODriver> &driver) {
    std::shared_ptr<VROMaterial> mat = material;
    renderElement(geometry, elementIndex, transform, normalMatrix, opacity,
                  mat, context, driver);
}

void VROGeometrySubstrateMetal::renderElement(const VROGeometry &geometry,
                                              int elementIndex,
                                              VROMatrix4f transform,
                                              VROMatrix4f normalMatrix,
                                              float opacity,
                                              std::shared_ptr<VROMaterial> &material,
                                              const VRORenderContext &context,
                                              std::shared_ptr<VRODriver> &driver) {
    std::shared_ptr<VRODriverMetal> metalDriver =
        std::dynamic_pointer_cast<VRODriverMetal>(driver);
    id <MTLRenderCommandEncoder> encoder =
        metalDriver ? metalDriver->getActiveRenderEncoder() : nil;
    if (encoder == nil) {
        if (!_warnedEncoder) {
            pwarn("VROGeometrySubstrateMetal: no active render encoder — draw skipped");
            _warnedEncoder = true;
        }
        return;
    }

    VROMatrix4f viewMatrix = context.getViewMatrix();
    VROMatrix4f projectionMatrix = context.getProjectionMatrix();
    if (geometry.isCameraEnclosure()) {
        viewMatrix = context.getEnclosureViewMatrix();
    }
    if (geometry.isScreenSpace()) {
        viewMatrix = VROMatrix4f();
        projectionMatrix = context.getOrthographicMatrix();
    }

    VROMaterialSubstrateMetal *substrate =
        static_cast<VROMaterialSubstrateMetal *>(material->getSubstrate(driver));
    substrate->bindView(transform, viewMatrix, projectionMatrix, normalMatrix,
                        context.getCamera().getPosition(), context.getEyeType(), context);
    substrate->bindGeometry(opacity, geometry);

    passert (elementIndex < _elements.size() && elementIndex >= 0);
    VROGeometryElementMetal &element = _elements[elementIndex];

    id <MTLRenderPipelineState> pipeline = getPipelineState(elementIndex, substrate, *metalDriver);
    if (pipeline == nil) {
        return;
    }

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:getDepthStencilState(*metalDriver)];
    // Viro geometry is GL-wound (CCW front faces); Metal defaults to CW, so
    // without this, back-face culling keeps the far faces and the mesh draws
    // inside-out.
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setCullMode:VRODriverMetal::toMTLCullMode(metalDriver->getCullMode())];

    for (const VROVertexDescriptorMetal *vd : descriptorsForElement(elementIndex)) {
        [encoder setVertexBuffer:vd->buffer offset:0 atIndex:vd->bufferIndex];
    }
    [encoder setVertexBytes:&substrate->getViewUniforms()
                     length:sizeof(VROMetalViewUniforms)
                    atIndex:kVROMetalViewUniformsIndex];
    [encoder setFragmentBytes:&substrate->getMaterialUniforms()
                       length:sizeof(VROMetalMaterialUniforms)
                      atIndex:0];
    [encoder setFragmentTexture:substrate->getDiffuseTexture() atIndex:0];

    [encoder drawIndexedPrimitives:element.primitiveType
                        indexCount:element.indexCount
                         indexType:element.indexType
                       indexBuffer:element.buffer
                 indexBufferOffset:element.indexBufferOffset];
}

void VROGeometrySubstrateMetal::renderSilhouette(const VROGeometry &geometry,
                                                 VROMatrix4f transform,
                                                 std::shared_ptr<VROMaterial> &material,
                                                 const VRORenderContext &context,
                                                 std::shared_ptr<VRODriver> &driver) {
    VROMatrix4f normalMatrix;
    for (int i = 0; i < (int)_elements.size(); i++) {
        renderElement(geometry, i, transform, normalMatrix, 1.0, material, context, driver);
    }
}

void VROGeometrySubstrateMetal::renderSilhouetteTextured(const VROGeometry &geometry,
                                                         int elementIndex,
                                                         VROMatrix4f transform,
                                                         std::shared_ptr<VROMaterial> &material,
                                                         const VRORenderContext &context,
                                                         std::shared_ptr<VRODriver> &driver) {
    VROMatrix4f normalMatrix;
    renderElement(geometry, elementIndex, transform, normalMatrix, 1.0, material, context, driver);
}

#endif // VRO_METAL
