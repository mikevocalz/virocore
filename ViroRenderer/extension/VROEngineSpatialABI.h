//
// VROEngineSpatialABI.h
//
// Language-neutral rigid/shared coordinate frame ABI for co-location, anchors,
// and spatial backends. Networking and app replication remain above this layer.
//

#ifndef VRO_ENGINE_SPATIAL_ABI_H
#define VRO_ENGINE_SPATIAL_ABI_H

#include <stdint.h>
#include "VROEngineABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_SPATIAL_ABI_MAJOR 0u
#define VRO_ENGINE_SPATIAL_ABI_MINOR 1u

typedef enum VROEngineTrackingState {
    VRO_ENGINE_TRACKING_UNAVAILABLE = 0,
    VRO_ENGINE_TRACKING_LIMITED = 1,
    VRO_ENGINE_TRACKING_NORMAL = 2
} VROEngineTrackingState;

typedef struct VROEngineRigidTransform {
    uint32_t struct_size;
    uint32_t flags;
    VROEngineVec3 translation;
    VROEngineQuat rotation;
} VROEngineRigidTransform;

#define VRO_ENGINE_RIGID_TRANSFORM_V0_1_SIZE 36u

typedef struct VROEngineSharedFrame {
    uint32_t struct_size;
    uint32_t tracking_state;
    uint64_t frame_id;
    uint64_t revision;
    uint64_t timestamp_ns;
    VROEngineRigidTransform local_from_shared;
    float confidence;
    uint32_t flags;
} VROEngineSharedFrame;

/*
 * The v0.1 prefix is frozen. Minor versions may append fields only.
 * Current natural C layout is 80 bytes on the supported 64-bit native ABIs.
 */
#define VRO_ENGINE_SHARED_FRAME_V0_1_SIZE 80u

VROEngineStatusCode viro_engine_transform_identity(
    VROEngineRigidTransform *out_transform);

VROEngineStatusCode viro_engine_transform_compose(
    const VROEngineRigidTransform *parent_from_mid,
    const VROEngineRigidTransform *mid_from_child,
    VROEngineRigidTransform *out_parent_from_child);

VROEngineStatusCode viro_engine_transform_invert(
    const VROEngineRigidTransform *parent_from_child,
    VROEngineRigidTransform *out_child_from_parent);

VROEngineStatusCode viro_engine_transform_point(
    const VROEngineRigidTransform *parent_from_child,
    const VROEngineVec3 *child_point,
    VROEngineVec3 *out_parent_point);

VROEngineStatusCode viro_engine_shared_frame_validate(
    const VROEngineSharedFrame *frame);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(VROEngineRigidTransform) == VRO_ENGINE_RIGID_TRANSFORM_V0_1_SIZE,
              "Rigid transform ABI v0.1 must stay 36 bytes");
static_assert(sizeof(VROEngineSharedFrame) == VRO_ENGINE_SHARED_FRAME_V0_1_SIZE,
              "Shared frame ABI v0.1 must stay 80 bytes");
#endif

#endif
