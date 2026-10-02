#pragma once

#include "VROEngineContract.h"
#include "VROEngineInput.h"

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VROEngineSpatialFrameKind {
    VRO_ENGINE_FRAME_LOCAL = 0,
    VRO_ENGINE_FRAME_STAGE = 1,
    VRO_ENGINE_FRAME_ANCHOR = 2,
    VRO_ENGINE_FRAME_SHARED = 3,
    VRO_ENGINE_FRAME_GEOSPATIAL = 4
} VROEngineSpatialFrameKind;

typedef struct VROEngineSpatialTransform {
    uint32_t struct_size;
    uint32_t frame_kind;
    uint64_t frame_id;
    uint64_t timestamp_ns;
    VROEnginePose pose;
    float scale[3];
} VROEngineSpatialTransform;

VROEngineSpatialTransform vro_engine_identity_transform(uint32_t frame_kind, uint64_t frame_id);

#ifdef __cplusplus
}
#endif
