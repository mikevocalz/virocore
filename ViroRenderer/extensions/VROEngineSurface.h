#pragma once

#include "VROEngineContract.h"

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VROEngineSurfaceKind {
    VRO_ENGINE_SURFACE_GPU_PANEL = 0,
    VRO_ENGINE_SURFACE_RIVE = 1,
    VRO_ENGINE_SURFACE_THREE = 2,
    VRO_ENGINE_SURFACE_CAMERA = 3,
    VRO_ENGINE_SURFACE_VIDEO = 4
} VROEngineSurfaceKind;

typedef enum VROEnginePixelFormat {
    VRO_ENGINE_PIXEL_UNKNOWN = 0,
    VRO_ENGINE_PIXEL_RGBA8 = 1,
    VRO_ENGINE_PIXEL_BGRA8 = 2,
    VRO_ENGINE_PIXEL_RGBA16F = 3
} VROEnginePixelFormat;

typedef struct VROEngineSurfaceDescriptor {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    VROEngineHandle resource;
    uint64_t frame_id;
    uint64_t timestamp_ns;
} VROEngineSurfaceDescriptor;

VROEngineStatus vro_engine_validate_surface(const VROEngineSurfaceDescriptor *surface);

#ifdef __cplusplus
}
#endif
