//
//  VROSceneGraphMetalBridge.mm — see header for purpose.
//
//  Uses real ViroRenderer classes: VROBox produces the geometry
//  (VROGeometrySource/VROGeometryElement/VROData), VROMatrix4f/VROQuaternion
//  do the pose→view and FOV→projection math identical to
//  VROSceneRendererOpenXR.cpp::xrPoseToMatrix / xrFovToProjection / renderEye.
//
//  SOT-KEYWORDS: openxr, metal, vrobox, bridge, mvp
//

#include "VROSceneGraphMetalBridge.h"

#include "VROBox.h"
#include "VROGeometry.h"
#include "VROGeometrySource.h"
#include "VROGeometryElement.h"
#include "VROData.h"
#include "VROShapeUtils.h"
#include "VROMatrix4f.h"
#include "VROVector3f.h"
#include "VROQuaternion.h"
#include "VROMath.h"

#include <cmath>
#include <cstdio>
#include <vector>

// Same vertex format as VROSceneRendererMetalOpenXR.mm's MSL shader:
// interleaved float3 position + float3 color.
struct BridgeVertex {
    float pos[3];
    float col[3];
};

// Identical to VROSceneRendererOpenXR.cpp::xrPoseToMatrix.
static VROMatrix4f xrPoseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f rot = q.getMatrix();
    rot[12] = pose.position.x;
    rot[13] = pose.position.y;
    rot[14] = pose.position.z;
    return rot;
}

// Identical to VROSceneRendererOpenXR.cpp::xrFovToProjection.
static VROMatrix4f xrFovToProjection(const XrFovf &fov,
                                     float nearZ = 0.1f,
                                     float farZ  = 1000.0f) {
    const float left   = tanf(fov.angleLeft);
    const float right  = tanf(fov.angleRight);
    const float down   = tanf(fov.angleDown);
    const float up     = tanf(fov.angleUp);

    const float w  =  right - left;
    const float h  =  up    - down;
    const float q  = -(farZ + nearZ) / (farZ - nearZ);
    const float qn = -2.0f * farZ * nearZ / (farZ - nearZ);

    float m[16] = {};
    m[0]  =  2.0f / w;
    m[5]  =  2.0f / h;
    m[8]  =  (right + left) / w;
    m[9]  =  (up    + down) / h;
    m[10] =  q;
    m[11] = -1.0f;
    m[14] =  qn;
    return VROMatrix4f(m);
}

bool VROBuildBoxGeometry(id<MTLDevice> device,
                         id<MTLBuffer> *vertexBuffer,
                         id<MTLBuffer> *indexBuffer,
                         uint32_t *indexCount) {
    // Real Viro geometry: a 0.5 m cube.
    std::shared_ptr<VROBox> box = VROBox::createBox(0.5f, 0.5f, 0.5f);

    std::shared_ptr<VROGeometrySource> posSrc, nrmSrc;
    for (auto &src : box->getGeometrySources()) {
        if (src->getSemantic() == VROGeometrySourceSemantic::Vertex) posSrc = src;
        if (src->getSemantic() == VROGeometrySourceSemantic::Normal) nrmSrc = src;
    }
    if (!posSrc || !nrmSrc || box->getGeometryElements().empty()) {
        std::fputs("VROBox: missing vertex/normal source or elements\n", stderr);
        return false;
    }
    const int numVertices = posSrc->getVertexCount();

    // Both sources share one interleaved VROShapeVertexLayout buffer; read
    // position + normal through the sources' offset/stride rather than
    // assuming the layout.
    const uint8_t *posBase = (const uint8_t *)posSrc->getData()->getData()
                           + posSrc->getDataOffset();
    const uint8_t *nrmBase = (const uint8_t *)nrmSrc->getData()->getData()
                           + nrmSrc->getDataOffset();

    std::vector<BridgeVertex> verts(numVertices);
    for (int i = 0; i < numVertices; ++i) {
        const float *p = (const float *)(posBase + i * posSrc->getDataStride());
        const float *n = (const float *)(nrmBase + i * nrmSrc->getDataStride());
        verts[i].pos[0] = p[0]; verts[i].pos[1] = p[1]; verts[i].pos[2] = p[2];
        // Bake normal direction into color so unlit faces read distinctly.
        verts[i].col[0] = fabsf(n[0]) * 0.7f + fabsf(n[1]) * 0.1f;
        verts[i].col[1] = fabsf(n[1]) * 0.8f + 0.1f;
        verts[i].col[2] = fabsf(n[2]) * 0.9f + fabsf(n[0]) * 0.2f;
    }

    std::shared_ptr<VROGeometryElement> elem = box->getGeometryElements()[0];
    if (elem->getPrimitiveType() != VROGeometryPrimitiveType::Triangle ||
        elem->getBytesPerIndex() != (int)sizeof(int32_t)) {
        std::fputs("VROBox: unexpected element format\n", stderr);
        return false;
    }
    const uint32_t count = elem->getPrimitiveCount() * 3;
    const int32_t *idx = (const int32_t *)elem->getData()->getData();

    *vertexBuffer = [device newBufferWithBytes:verts.data()
                                        length:verts.size() * sizeof(BridgeVertex)
                                       options:MTLResourceStorageModeShared];
    *indexBuffer = [device newBufferWithBytes:idx
                                       length:count * sizeof(int32_t)
                                      options:MTLResourceStorageModeShared];
    *indexCount = count;
    std::printf("VROBox geometry: %d vertices, %u indices\n", numVertices, count);
    return *vertexBuffer != nil && *indexBuffer != nil;
}

void VROComputeEyeUniforms(int eye, XrPosef pose, XrFovf fov, int frame,
                         float *outMVP16) {
    VROMatrix4f view = xrPoseToMatrix(pose).invert();
    VROMatrix4f proj = xrFovToProjection(fov);

    // Anchor the box 1.5 m in front of the located head pose (camera forward
    // is -Z column of the pose matrix) so it is always in view, and spin it.
    VROMatrix4f headPose = xrPoseToMatrix(pose);
    float angle = frame * 0.01f;
    VROMatrix4f model;
    VROMatrix4f rot;
    // Rotation about Y, column-major.
    float c = cosf(angle), s = sinf(angle);
    float rm[16] = { c, 0, -s, 0,
                     0, 1,  0, 0,
                     s, 0,  c, 0,
                     0, 0,  0, 1 };
    rot = VROMatrix4f(rm);
    model[12] = headPose[12] - headPose[8]  * 1.5f; // pos + forward * 1.5
    model[13] = headPose[13] - headPose[9]  * 1.5f;
    model[14] = headPose[14] - headPose[10] * 1.5f;
    model = model.multiply(rot);

    VROMatrix4f mvp = proj.multiply(view).multiply(model);
    const float *m = mvp.getArray();
    for (int i = 0; i < 16; ++i) outMVP16[i] = m[i];
}
