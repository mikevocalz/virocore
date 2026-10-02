//
// VROEngineSpatialABI.cpp
//

#include "VROEngineSpatialABI.h"

#include <cmath>

namespace {

bool writableTransform(const VROEngineRigidTransform *t) {
    return t != nullptr && t->struct_size >= VRO_ENGINE_RIGID_TRANSFORM_V0_1_SIZE;
}

bool validTransform(const VROEngineRigidTransform *t) {
    if (!writableTransform(t)) {
        return false;
    }
    const auto &p = t->translation;
    const auto &q = t->rotation;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        !std::isfinite(q.x) || !std::isfinite(q.y) ||
        !std::isfinite(q.z) || !std::isfinite(q.w)) {
        return false;
    }
    const float n2 = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(n2) && n2 > 0.0f;
}

VROEngineQuat normalize(VROEngineQuat q) {
    const float n2 = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (n2 <= 0.0f || !std::isfinite(n2)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float inv = 1.0f / std::sqrt(n2);
    return {q.x*inv, q.y*inv, q.z*inv, q.w*inv};
}

VROEngineQuat multiply(VROEngineQuat a, VROEngineQuat b) {
    return normalize({
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    });
}

VROEngineQuat conjugate(VROEngineQuat q) {
    q = normalize(q);
    return {-q.x, -q.y, -q.z, q.w};
}

VROEngineVec3 rotate(VROEngineQuat q, VROEngineVec3 v) {
    q = normalize(q);
    const VROEngineVec3 u{q.x, q.y, q.z};
    const float s = q.w;

    const float dotUV = u.x*v.x + u.y*v.y + u.z*v.z;
    const float dotUU = u.x*u.x + u.y*u.y + u.z*u.z;
    const VROEngineVec3 cross{
        u.y*v.z - u.z*v.y,
        u.z*v.x - u.x*v.z,
        u.x*v.y - u.y*v.x
    };

    return {
        2.0f*dotUV*u.x + (s*s - dotUU)*v.x + 2.0f*s*cross.x,
        2.0f*dotUV*u.y + (s*s - dotUU)*v.y + 2.0f*s*cross.y,
        2.0f*dotUV*u.z + (s*s - dotUU)*v.z + 2.0f*s*cross.z
    };
}

VROEngineVec3 add(VROEngineVec3 a, VROEngineVec3 b) {
    return {a.x+b.x, a.y+b.y, a.z+b.z};
}

VROEngineVec3 negate(VROEngineVec3 v) {
    return {-v.x, -v.y, -v.z};
}

} // namespace

extern "C" VROEngineStatusCode viro_engine_transform_identity(
    VROEngineRigidTransform *out_transform) {
    if (!writableTransform(out_transform)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    out_transform->struct_size = sizeof(VROEngineRigidTransform);
    out_transform->flags = 0;
    out_transform->translation = {0.0f, 0.0f, 0.0f};
    out_transform->rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_transform_compose(
    const VROEngineRigidTransform *parent_from_mid,
    const VROEngineRigidTransform *mid_from_child,
    VROEngineRigidTransform *out_parent_from_child) {
    if (!validTransform(parent_from_mid) ||
        !validTransform(mid_from_child) ||
        !writableTransform(out_parent_from_child)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    // Snapshot both inputs before writing the output so in-place composition is safe.
    const VROEngineRigidTransform parent = *parent_from_mid;
    const VROEngineRigidTransform child = *mid_from_child;

    VROEngineRigidTransform result{};
    result.struct_size = sizeof(result);
    result.flags = parent.flags | child.flags;
    result.rotation = multiply(parent.rotation, child.rotation);
    result.translation = add(
        parent.translation,
        rotate(parent.rotation, child.translation));
    *out_parent_from_child = result;
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_transform_invert(
    const VROEngineRigidTransform *parent_from_child,
    VROEngineRigidTransform *out_child_from_parent) {
    if (!validTransform(parent_from_child) || !writableTransform(out_child_from_parent)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    const VROEngineQuat inverseRotation = conjugate(parent_from_child->rotation);
    out_child_from_parent->struct_size = sizeof(VROEngineRigidTransform);
    out_child_from_parent->flags = parent_from_child->flags;
    out_child_from_parent->rotation = inverseRotation;
    out_child_from_parent->translation =
        rotate(inverseRotation, negate(parent_from_child->translation));
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_transform_point(
    const VROEngineRigidTransform *parent_from_child,
    const VROEngineVec3 *child_point,
    VROEngineVec3 *out_parent_point) {
    if (!validTransform(parent_from_child) ||
        child_point == nullptr ||
        out_parent_point == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *out_parent_point = add(
        parent_from_child->translation,
        rotate(parent_from_child->rotation, *child_point));
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_shared_frame_validate(
    const VROEngineSharedFrame *frame) {
    if (frame == nullptr ||
        frame->struct_size < VRO_ENGINE_SHARED_FRAME_V0_1_SIZE ||
        !validTransform(&frame->local_from_shared) ||
        frame->tracking_state > VRO_ENGINE_TRACKING_NORMAL ||
        !std::isfinite(frame->confidence)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    return VRO_ENGINE_STATUS_OK;
}
