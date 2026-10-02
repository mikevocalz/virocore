#pragma once

#include "VROEngineContract.h"
#include "VROEngineSurface.h"

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VROEngineMediaKind {
    VRO_ENGINE_MEDIA_CAMERA = 0,
    VRO_ENGINE_MEDIA_VIDEO = 1,
    VRO_ENGINE_MEDIA_RECORDING = 2
} VROEngineMediaKind;

typedef struct VROEngineMediaFrame {
    uint32_t struct_size;
    uint32_t kind;
    uint64_t timestamp_ns;
    VROEngineSurfaceDescriptor surface;
} VROEngineMediaFrame;

VROEngineStatus vro_engine_validate_media_frame(const VROEngineMediaFrame *frame);

#ifdef __cplusplus
}
#endif
