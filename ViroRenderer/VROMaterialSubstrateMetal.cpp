//
//  VROMaterialSubstrateMetal.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/30/15.
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

#include "VROMaterialSubstrateMetal.h"
#if VRO_METAL

#include "VROMaterial.h"
#include "VROMaterialVisual.h"
#include "VRODriverMetal.h"
#include "VROGeometry.h"
#include "VROSortKey.h"
#include "VROTexture.h"
#include "VRORenderContext.h"
#include "VROLog.h"
#include "VROShaderModifier.h"

// Minimal MSL for the milestone substrate: a constant (unlit) pipeline with
// fixed hemisphere shading so lit faces read distinctly. Lighting-model and
// texture variants can be added without changing the binding contract.
static const char *kVROMetalShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VROMetalViewUniforms {
    float4x4 modelview_projection;
    float4x4 normal_matrix;
};

struct VROMetalMaterialUniforms {
    float4 diffuse_color;
    float  opacity;
    float3 _pad;
};

struct VROVertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 texcoord [[attribute(3)]];
};

struct VROVertexInPosOnly {
    float3 position [[attribute(0)]];
};

struct VROVertexOut {
    float4 position [[position]];
    float  shade;
};

constant float3 kLightDirection = { 0.4f, 0.8f, 0.45f };

vertex VROVertexOut vro_constant_vertex(VROVertexIn in [[stage_in]],
                                        constant VROMetalViewUniforms &uniforms [[buffer(15)]]) {
    VROVertexOut out;
    out.position = uniforms.modelview_projection * float4(in.position, 1.0);
    float3 n = normalize((uniforms.normal_matrix * float4(in.normal, 0.0)).xyz);
    out.shade = 0.35 + 0.65 * max(dot(n, normalize(kLightDirection)), 0.0);
    return out;
}

vertex VROVertexOut vro_constant_vertex_posonly(VROVertexInPosOnly in [[stage_in]],
                                                constant VROMetalViewUniforms &uniforms [[buffer(15)]]) {
    VROVertexOut out;
    out.position = uniforms.modelview_projection * float4(in.position, 1.0);
    out.shade = 1.0;
    return out;
}

fragment float4 vro_constant_fragment(VROVertexOut in [[stage_in]],
                                      constant VROMetalMaterialUniforms &material [[buffer(0)]]) {
    return float4(material.diffuse_color.rgb * in.shade,
                  material.diffuse_color.a * material.opacity);
}
)MSL";

VROMaterialSubstrateMetal::VROMaterialSubstrateMetal(const VROMaterial &material,
                                                     VRODriverMetal &driver) :
    _material(material),
    _vertexProgramFull(nil),
    _vertexProgramPosOnly(nil),
    _fragmentProgram(nil),
    _warnedLightingModel(false),
    _warnedTextures(false),
    _warnedModifiers(false) {

    _viewUniforms = {};
    _materialUniforms = {};

    // The driver caches one shared library compiled from the embedded source;
    // the first material substrate built compiles it.
    id <MTLLibrary> library = driver.getLibrary();
    if (library == nil) {
        library = driver.newLibraryWithSource(kVROMetalShaderSource);
        driver.setLibrary(library);
    }
    if (library == nil) {
        pwarn("VROMaterialSubstrateMetal: failed to compile shader library; material will not render");
        return;
    }

    _vertexProgramFull  = [library newFunctionWithName:@"vro_constant_vertex"];
    _vertexProgramPosOnly = [library newFunctionWithName:@"vro_constant_vertex_posonly"];
    _fragmentProgram    = [library newFunctionWithName:@"vro_constant_fragment"];

    if (!_vertexProgramFull || !_vertexProgramPosOnly || !_fragmentProgram) {
        pwarn("VROMaterialSubstrateMetal: shader entry points missing from library");
    }

    if (_material.getLightingModel() != VROLightingModel::Constant) {
        // Logged once per material; rendering proceeds with the constant pipeline.
        pwarn("VROMaterialSubstrateMetal: lighting model %d unsupported; rendering with constant pipeline",
              (int)_material.getLightingModel());
        _warnedLightingModel = true;
    }
    if (!_material.getShaderModifiers().empty()) {
        pwarn("VROMaterialSubstrateMetal: %zu shader modifiers ignored (unsupported)",
              _material.getShaderModifiers().size());
        _warnedModifiers = true;
    }
}

VROMaterialSubstrateMetal::~VROMaterialSubstrateMetal() {
    _vertexProgramFull = nil;
    _vertexProgramPosOnly = nil;
    _fragmentProgram = nil;
}

id <MTLFunction> VROMaterialSubstrateMetal::getVertexProgram(VROMetalVertexVariant variant) const {
    return (variant == VROMetalVertexVariant::Full) ? _vertexProgramFull : _vertexProgramPosOnly;
}

bool VROMaterialSubstrateMetal::bindShader(int lightsHash,
                                           const std::vector<std::shared_ptr<VROLight>> &lights,
                                           const VRORenderContext &context,
                                           std::shared_ptr<VRODriver> &driver) {
    if (!lights.empty() && !_warnedLightingModel) {
        pwarn("VROMaterialSubstrateMetal: %zu lights ignored (constant pipeline)", lights.size());
        _warnedLightingModel = true;
    }
    return _vertexProgramFull != nil && _fragmentProgram != nil;
}

void VROMaterialSubstrateMetal::bindProperties(std::shared_ptr<VRODriver> &driver) {
    VROVector4f diffuse = _material.getDiffuse().getColor();
    _materialUniforms.diffuse_color[0] = diffuse.x;
    _materialUniforms.diffuse_color[1] = diffuse.y;
    _materialUniforms.diffuse_color[2] = diffuse.z;
    _materialUniforms.diffuse_color[3] = diffuse.w;
    _materialUniforms.opacity = _material.getTransparency();
}

void VROMaterialSubstrateMetal::bindGeometry(float opacity, const VROGeometry &geometry) {
    _materialUniforms.opacity *= opacity;
}

void VROMaterialSubstrateMetal::bindView(VROMatrix4f modelMatrix, VROMatrix4f viewMatrix,
                                         VROMatrix4f projectionMatrix, VROMatrix4f normalMatrix,
                                         VROVector3f cameraPosition, VROEyeType eyeType,
                                         const VRORenderContext &context) {
    VROMatrix4f mvp = projectionMatrix.multiply(viewMatrix).multiply(modelMatrix);
    memcpy(_viewUniforms.modelview_projection, mvp.getArray(), sizeof(_viewUniforms.modelview_projection));
    memcpy(_viewUniforms.normal_matrix, normalMatrix.getArray(), sizeof(_viewUniforms.normal_matrix));
}

void VROMaterialSubstrateMetal::updateTextures() {
    if (!_warnedTextures) {
        pwarn("VROMaterialSubstrateMetal: updateTextures() ignored (texture binding unsupported)");
        _warnedTextures = true;
    }
}

void VROMaterialSubstrateMetal::updateSortKey(VROSortKey &key,
                                              const std::vector<std::shared_ptr<VROLight>> &lights,
                                              const VRORenderContext &context,
                                              std::shared_ptr<VRODriver> driver) {
    key.materialRenderingOrder = _material.getRenderingOrder();
    // Single stable shader identity for the constant pipeline.
    key.shader = 1;
    key.textures = 0;
}

#endif // VRO_METAL
