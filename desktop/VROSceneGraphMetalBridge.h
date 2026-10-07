//
//  VROSceneGraphMetalBridge.h
//
//  Bridge between the desktop OpenXR host and the real ViroRenderer scene
//  classes. Pulls vertex/index data out of a live VROBox (VROGeometry
//  sources/elements) into Metal buffers, and computes per-eye MVP uniforms
//  with VROMatrix4f using the same pose→view and FOV→projection math as
//  VROSceneRendererOpenXR::renderEye.
//
//  Compiled only when VIRO_DESKTOP_SCENE is defined.
//
//  SOT-KEYWORDS: openxr, metal, vrobox, bridge, mvp
//

#ifndef VROSceneGraphMetalBridge_h
#define VROSceneGraphMetalBridge_h

#import <Metal/Metal.h>
#include <openxr/openxr.h>
#include <cstdint>

// Builds Metal vertex/index buffers from a real VROBox. Vertex data is read
// out of the box's VROGeometrySource buffers (VROShapeVertexLayout layout,
// 48 B stride); indices come from its VROGeometryElement. Colors are baked
// per-vertex from |normal| so faces are distinguishable without lighting.
// Returns false if the geometry could not be read.
bool VROBuildBoxGeometry(id<MTLDevice> device,
                         id<MTLBuffer> *vertexBuffer,
                         id<MTLBuffer> *indexBuffer,
                         uint32_t *indexCount);

// Computes the column-major MVP for `eye` from xrLocateViews output, matching
// VROSceneRendererOpenXR: view = xrPoseToMatrix(pose).invert(),
// proj = xrFovToProjection(fov). A VROBox is placed ahead of the located head
// pose and rotated over `frame` so the cube is always in view.
void VROComputeEyeUniforms(int eye, XrPosef pose, XrFovf fov, int frame,
                         float *outMVP16);

#endif /* VROSceneGraphMetalBridge_h */
