#ifndef VRO_ENGINE_SPATIAL_BACKEND_H
#define VRO_ENGINE_SPATIAL_BACKEND_H

#include <stdint.h>

#include "VROEngineBackendSelector.h"
#include "VROEngineSpatialABI.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t viro_engine_spatial_backend_availability(void);
VROEngineBackendKind viro_engine_spatial_backend_resolve(void);

VROEngineStatusCode viro_engine_transform_identity_selected(
    VROEngineRigidTransform *out_transform);
VROEngineStatusCode viro_engine_transform_compose_selected(
    const VROEngineRigidTransform *parent_from_mid,
    const VROEngineRigidTransform *mid_from_child,
    VROEngineRigidTransform *out_parent_from_child);
VROEngineStatusCode viro_engine_transform_invert_selected(
    const VROEngineRigidTransform *parent_from_child,
    VROEngineRigidTransform *out_child_from_parent);
VROEngineStatusCode viro_engine_transform_point_selected(
    const VROEngineRigidTransform *parent_from_child,
    const VROEngineVec3 *child_point,
    VROEngineVec3 *out_parent_point);
VROEngineStatusCode viro_engine_shared_frame_validate_selected(
    const VROEngineSharedFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
