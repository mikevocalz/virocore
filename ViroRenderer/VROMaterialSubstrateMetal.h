//
//  VROMaterialSubstrateMetal.h
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

#ifndef VROMaterialSubstrateMetal_h
#define VROMaterialSubstrateMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include "VROMaterialSubstrate.h"
#include "VROMatrix4f.h"
#include <Metal/Metal.h>
#include <cstddef>
#include <vector>
#include <memory>

class VROMaterial;
class VRODriverMetal;
class VROConcurrentBuffer;
class VROVector3f;
class VROTexture;
enum class VROEyeType;

/*
 Uniform block consumed by the vertex functions (bound at vertex buffer
 index kVROMetalViewUniformsIndex). Must match the MSL struct of the same
 name in VROMaterialSubstrateMetal.cpp.
 */
struct VROMetalViewUniforms {
    float modelview_projection[16];
    float normal_matrix[16];
    float model_matrix[16];
};

/*
 One scene light in world space. color_type.rgb is color * intensity / 1000
 (Viro's 1000 = unit intensity) and .w is the VROLightType. Must match
 VROMetalLight in the MSL source.
 */
struct VROMetalLight {
    float color_type[4];
    float direction[4];
    float position[4];
};

static const int kVROMetalMaxLights = 8;

/*
 Uniform block consumed by the fragment functions (bound at fragment buffer
 index 0). Must match VROMetalMaterialUniforms in the MSL source.
 */
struct VROMetalMaterialUniforms {
    float diffuse_color[4];
    float opacity;
    float lit;                  // 0 for VROLightingModel::Constant
    float has_diffuse_texture;
    float light_count;
    float encode_srgb;          // 1 when the color target is not *_sRGB
    float _pad[3];
    VROMetalLight lights[kVROMetalMaxLights];
};
static_assert(offsetof(VROMetalMaterialUniforms, lights) == 48,
              "lights[] must start at byte 48 to match the MSL struct");
static_assert(sizeof(VROMetalLight) == 48, "VROMetalLight stride must match MSL");

/*
 Index (into the vertex shader's buffer table) at which VROMetalViewUniforms
 is bound. Geometry vertex buffers occupy indices 0..14.
 */
static const int kVROMetalViewUniformsIndex = 15;

/*
 Which vertex-entry variant a material provides. The geometry substrate
 picks the variant matching the attributes actually present in the geometry:
 full (position+normal+texcoord) or position-only.
 */
enum class VROMetalVertexVariant {
    Full,
    PositionOnly
};

/*
 Metal representation of a VROMaterial.

 Supported: diffuse color and diffuse texture; Constant (unlit) and diffuse
 lighting from up to kVROMetalMaxLights scene lights (ambient, directional,
 omni, spot), with Lambert, Blinn and Phong all shaded as Lambert. Specular,
 normal maps, PBR, shader modifiers and dynamic uniforms are skipped WITH a
 logged warning (no silent no-ops).
 */
class VROMaterialSubstrateMetal : public VROMaterialSubstrate {

public:

    VROMaterialSubstrateMetal(const VROMaterial &material,
                              VRODriverMetal &driver);
    virtual ~VROMaterialSubstrateMetal();

    // VROMaterialSubstrate
    bool bindShader(int lightsHash,
                    const std::vector<std::shared_ptr<VROLight>> &lights,
                    const VRORenderContext &context,
                    std::shared_ptr<VRODriver> &driver) override;
    void bindProperties(std::shared_ptr<VRODriver> &driver) override;
    void bindGeometry(float opacity, const VROGeometry &geometry) override;
    void bindView(VROMatrix4f modelMatrix, VROMatrix4f viewMatrix,
                  VROMatrix4f projectionMatrix, VROMatrix4f normalMatrix,
                  VROVector3f cameraPosition, VROEyeType eyeType,
                  const VRORenderContext &context) override;
    void updateTextures() override;
    void updateSortKey(VROSortKey &key, const std::vector<std::shared_ptr<VROLight>> &lights,
                       const VRORenderContext &context,
                       std::shared_ptr<VRODriver> driver) override;

    /*
     Vertex/fragment functions for this material. The vertex function is
     chosen by the geometry substrate based on the attributes present.
     */
    id <MTLFunction> getVertexProgram(VROMetalVertexVariant variant) const;
    id <MTLFunction> getFragmentProgram() const {
        return _fragmentProgram;
    }

    /*
     The uniform bytes most recently produced by bindView() / bindGeometry() /
     bindProperties(); the geometry substrate binds them to the encoder.
     */
    const VROMetalViewUniforms &getViewUniforms() const {
        return _viewUniforms;
    }
    const VROMetalMaterialUniforms &getMaterialUniforms() const {
        return _materialUniforms;
    }

    const std::vector<std::shared_ptr<VROTexture>> &getTextures() const {
        return _textures;
    }

    /*
     Diffuse texture resolved by the last bindProperties(), or nil. The
     geometry substrate binds it at fragment texture index 0.
     */
    id <MTLTexture> getDiffuseTexture() const {
        return _diffuseTexture;
    }
    id <MTLSamplerState> getDiffuseSampler() const {
        return _diffuseSampler;
    }

private:

    const VROMaterial &_material;

    id <MTLFunction> _vertexProgramFull;
    id <MTLFunction> _vertexProgramPosOnly;
    id <MTLFunction> _fragmentProgram;

    VROMetalViewUniforms _viewUniforms;
    VROMetalMaterialUniforms _materialUniforms;

    std::vector<std::shared_ptr<VROTexture>> _textures;
    id <MTLTexture> _diffuseTexture;
    id <MTLSamplerState> _diffuseSampler;

    // Log-once guards so unsupported features are surfaced, not silently dropped.
    bool _warnedLightingModel;
    bool _warnedTextures;
    bool _warnedModifiers;
    bool _warnedLightCount;

};

#endif // VRO_METAL
#endif /* VROMaterialSubstrateMetal_h */
