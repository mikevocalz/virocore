#include "VROEngineSpatialBackend.h"

#if defined(VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE) && VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE
extern "C" {
int viro_eskiu_transform_identity(VROEngineRigidTransform *out_transform);
int viro_eskiu_transform_compose(
    VROEngineRigidTransform *parent_from_mid,
    VROEngineRigidTransform *mid_from_child,
    VROEngineRigidTransform *out_parent_from_child);
int viro_eskiu_transform_invert(
    VROEngineRigidTransform *parent_from_child,
    VROEngineRigidTransform *out_child_from_parent);
int viro_eskiu_transform_point(
    VROEngineRigidTransform *parent_from_child,
    VROEngineVec3 *child_point,
    VROEngineVec3 *out_parent_point);
int viro_eskiu_shared_frame_validate(VROEngineSharedFrame *frame);
}
#endif

extern "C" uint32_t viro_engine_spatial_backend_availability(void) {
    uint32_t available = VRO_ENGINE_BACKEND_AVAILABLE_CPP;
#if defined(VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE) && VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE
    available |= VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;
#endif
    return available;
}

extern "C" VROEngineBackendKind viro_engine_spatial_backend_resolve(void) {
    return viro_engine_backend_resolve(
        VRO_ENGINE_DOMAIN_SPATIAL,
        viro_engine_spatial_backend_availability());
}

extern "C" VROEngineStatusCode viro_engine_transform_identity_selected(
    VROEngineRigidTransform *out_transform) {
    const auto backend = viro_engine_spatial_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_transform_identity(out_transform);
    }
#if defined(VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE) && VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_transform_identity(out_transform));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_transform_compose_selected(
    const VROEngineRigidTransform *parent_from_mid,
    const VROEngineRigidTransform *mid_from_child,
    VROEngineRigidTransform *out_parent_from_child) {
    const auto backend = viro_engine_spatial_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_transform_compose(
            parent_from_mid, mid_from_child, out_parent_from_child);
    }
#if defined(VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE) && VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_transform_compose(
                const_cast<VROEngineRigidTransform *>(parent_from_mid),
                const_cast<VROEngineRigidTransform *>(mid_from_child),
                out_parent_from_child));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_transform_invert_selected(
    const VROEngineRigidTransform *parent_from_child,
    VROEngineRigidTransform *out_child_from_parent) {
    const auto backend = viro_engine_spatial_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_transform_invert(
            parent_from_child, out_child_from_parent);
    }
#if defined(VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE) && VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_transform_invert(
                const_cast<VROEngineRigidTransform *>(parent_from_child),
                out_child_from_parent));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_transform_point_selected(
    const VROEngineRigidTransform *parent_from_child,
    const VROEngineVec3 *child_point,
    VROEngineVec3 *out_parent_point) {
    const auto backend = viro_engine_spatial_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_transform_point(
            parent_from_child, child_point, out_parent_point);
    }
#if defined(VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE) && VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_transform_point(
                const_cast<VROEngineRigidTransform *>(parent_from_child),
                const_cast<VROEngineVec3 *>(child_point),
                out_parent_point));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_shared_frame_validate_selected(
    const VROEngineSharedFrame *frame) {
    const auto backend = viro_engine_spatial_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_shared_frame_validate(frame);
    }
#if defined(VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE) && VRO_ENGINE_ESKIU_SPATIAL_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_shared_frame_validate(
                const_cast<VROEngineSharedFrame *>(frame)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}
