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
#include "VROTextureSubstrateMetal.h"
#include "VROLight.h"
#include <algorithm>

// Minimal MSL for the milestone substrate: a constant (unlit) pipeline with
// fixed hemisphere shading so lit faces read distinctly. Lighting-model and
// texture variants can be added without changing the binding contract.
static const char *kVROMetalShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VROMetalViewUniforms {
    float4x4 modelview_projection;
    float4x4 normal_matrix;
    float4x4 model_matrix;
};

struct VROMetalLight {
    float4 color_type;   // rgb = color * intensity, w = VROLightType
    float4 direction;
    float4 position;
};

struct VROMetalMaterialUniforms {
    float4 diffuse_color;
    float  opacity;
    float  lit;
    float  has_diffuse_texture;
    float  light_count;
    float  encode_srgb;
    float  _pad0, _pad1, _pad2;  // scalars: a float3 here would be 16-aligned
    VROMetalLight lights[8];
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
    float3 world_position;
    float3 normal;
    float2 texcoord;
};

vertex VROVertexOut vro_constant_vertex(VROVertexIn in [[stage_in]],
                                        constant VROMetalViewUniforms &uniforms [[buffer(15)]]) {
    VROVertexOut out;
    out.position = uniforms.modelview_projection * float4(in.position, 1.0);
    out.world_position = (uniforms.model_matrix * float4(in.position, 1.0)).xyz;
    out.normal = (uniforms.normal_matrix * float4(in.normal, 0.0)).xyz;
    out.texcoord = in.texcoord;
    return out;
}

vertex VROVertexOut vro_constant_vertex_posonly(VROVertexInPosOnly in [[stage_in]],
                                                constant VROMetalViewUniforms &uniforms [[buffer(15)]]) {
    VROVertexOut out;
    out.position = uniforms.modelview_projection * float4(in.position, 1.0);
    out.world_position = (uniforms.model_matrix * float4(in.position, 1.0)).xyz;
    out.normal = float3(0.0, 0.0, 1.0);
    out.texcoord = float2(0.0);
    return out;
}

// Viro light types: 0 ambient, 1 directional, 2 omni, 3 spot.
static float3 vro_diffuse_light(constant VROMetalMaterialUniforms &m, float3 n, float3 p) {
    float3 total = float3(0.0);
    for (int i = 0; i < int(m.light_count); ++i) {
        constant VROMetalLight &l = m.lights[i];
        int type = int(l.color_type.w + 0.5);
        float3 c = l.color_type.rgb;
        if (type == 0) {
            total += c;
        } else if (type == 1) {
            total += c * max(dot(n, normalize(-l.direction.xyz)), 0.0);
        } else {
            float3 toLight = l.position.xyz - p;
            float d = length(toLight);
            // position.w = attenuation end distance; 0 means no falloff.
            float atten = l.position.w > 0.0 ? saturate(1.0 - d / l.position.w) : 1.0;
            total += c * atten * max(dot(n, toLight / max(d, 1e-4)), 0.0);
        }
    }
    return total;
}

fragment float4 vro_constant_fragment(VROVertexOut in [[stage_in]],
                                      constant VROMetalMaterialUniforms &material [[buffer(0)]],
                                      texture2d<float> diffuseTexture [[texture(0)]],
                                      sampler diffuseSampler [[sampler(0)]]) {
    float4 base = material.diffuse_color;
    if (material.has_diffuse_texture > 0.5) {
        base *= diffuseTexture.sample(diffuseSampler, in.texcoord);
    }
    float3 rgb = base.rgb;
    if (material.lit > 0.5) {
        rgb *= vro_diffuse_light(material, normalize(in.normal), in.world_position);
    }
    // Shading is linear. An *_sRGB target encodes on write; a Unorm target
    // (e.g. a default MTKView) needs the encode here.
    if (material.encode_srgb > 0.5) {
        rgb = pow(max(rgb, 0.0), float3(1.0 / 2.2));
    }
    return float4(rgb, base.a * material.opacity);
}
)MSL";

VROMaterialSubstrateMetal::VROMaterialSubstrateMetal(const VROMaterial &material,
                                                     VRODriverMetal &driver) :
    _material(material),
    _vertexProgramFull(nil),
    _vertexProgramPosOnly(nil),
    _fragmentProgram(nil),
    _diffuseTexture(nil),
    _diffuseSampler(nil),
    _warnedLightingModel(false),
    _warnedTextures(false),
    _warnedModifiers(false),
    _warnedLightCount(false) {

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

    VROLightingModel model = _material.getLightingModel();
    if (model == VROLightingModel::PhysicallyBased) {
        // Logged once per material; rendered with diffuse lighting instead.
        pwarn("VROMaterialSubstrateMetal: PBR unsupported; rendering with diffuse lighting");
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
    const size_t count = std::min(lights.size(), (size_t)kVROMetalMaxLights);
    if (lights.size() > count && !_warnedLightCount) {
        pwarn("VROMaterialSubstrateMetal: %zu lights, only the first %d are used",
              lights.size(), kVROMetalMaxLights);
        _warnedLightCount = true;
    }
    for (size_t i = 0; i < count; ++i) {
        const VROLight &light = *lights[i];
        VROMetalLight &out = _materialUniforms.lights[i];
        VROVector3f color = light.getColor() * (light.getIntensity() / 1000.0f);
        VROVector3f dir = light.getTransformedDirection();
        VROVector3f pos = light.getTransformedPosition();
        out.color_type[0] = color.x;
        out.color_type[1] = color.y;
        out.color_type[2] = color.z;
        out.color_type[3] = (float)light.getType();
        out.direction[0] = dir.x;
        out.direction[1] = dir.y;
        out.direction[2] = dir.z;
        out.direction[3] = 0;
        out.position[0] = pos.x;
        out.position[1] = pos.y;
        out.position[2] = pos.z;
        out.position[3] = light.getAttenuationEndDistance();
    }
    _materialUniforms.light_count = (float)count;
    return _vertexProgramFull != nil && _fragmentProgram != nil;
}

void VROMaterialSubstrateMetal::bindProperties(std::shared_ptr<VRODriver> &driver) {
    VROVector4f diffuse = _material.getDiffuse().getColor();
    _materialUniforms.diffuse_color[0] = diffuse.x;
    _materialUniforms.diffuse_color[1] = diffuse.y;
    _materialUniforms.diffuse_color[2] = diffuse.z;
    _materialUniforms.diffuse_color[3] = diffuse.w;
    _materialUniforms.opacity = _material.getTransparency();
    _materialUniforms.lit = _material.getLightingModel() == VROLightingModel::Constant ? 0 : 1;

    // Resolve the diffuse texture each bind: the material may swap textures
    // or finish loading one between frames.
    _diffuseTexture = nil;
    _diffuseSampler = nil;
    std::shared_ptr<VROTexture> texture = _material.getDiffuse().getTexture();
    if (texture) {
        VROTextureSubstrate *sub = texture->getSubstrate(0, driver, true);
        if (sub) {
            auto *metal = static_cast<VROTextureSubstrateMetal *>(sub);
            _diffuseTexture = metal->getTexture();
            _diffuseSampler = metal->getSampler();
        }
    }
    _materialUniforms.has_diffuse_texture =
        (_diffuseTexture != nil && _diffuseSampler != nil) ? 1 : 0;

    MTLPixelFormat target =
        std::static_pointer_cast<VRODriverMetal>(driver)->getColorPixelFormat();
    _materialUniforms.encode_srgb =
        (target == MTLPixelFormatBGRA8Unorm_sRGB || target == MTLPixelFormatRGBA8Unorm_sRGB) ? 0 : 1;
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
    memcpy(_viewUniforms.model_matrix, modelMatrix.getArray(), sizeof(_viewUniforms.model_matrix));
}

void VROMaterialSubstrateMetal::updateTextures() {
    // The diffuse texture is resolved per bind in bindProperties(); other
    // visuals (normal, specular, roughness, ...) are not sampled yet.
    if (!_warnedTextures && (_material.getNormal().getTexture() ||
                             _material.getSpecular().getTexture())) {
        pwarn("VROMaterialSubstrateMetal: only the diffuse texture is sampled; others ignored");
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
