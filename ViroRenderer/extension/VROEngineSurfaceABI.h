//
// VROEngineSurfaceABI.h
//
// Opaque render-surface / texture exchange ABI for external producers such as
// Rive, Three/WebGPU, camera textures and video.
//

#ifndef VRO_ENGINE_SURFACE_ABI_H
#define VRO_ENGINE_SURFACE_ABI_H

#include <stdint.h>
#include "VROEngineABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_SURFACE_ABI_MAJOR 0u
#define VRO_ENGINE_SURFACE_ABI_MINOR 1u

typedef enum VROEnginePixelFormat {
    VRO_ENGINE_PIXEL_UNKNOWN = 0,
    VRO_ENGINE_PIXEL_RGBA8_UNORM = 1,
    VRO_ENGINE_PIXEL_BGRA8_UNORM = 2,
    VRO_ENGINE_PIXEL_RGBA16_FLOAT = 3,
    VRO_ENGINE_PIXEL_DEPTH32_FLOAT = 4
} VROEnginePixelFormat;

typedef enum VROEngineSurfaceOrigin {
    VRO_ENGINE_SURFACE_ORIGIN_TOP_LEFT = 0,
    VRO_ENGINE_SURFACE_ORIGIN_BOTTOM_LEFT = 1
} VROEngineSurfaceOrigin;

typedef enum VROEngineSurfaceUsageBit {
    VRO_ENGINE_SURFACE_SAMPLED = 1u << 0,
    VRO_ENGINE_SURFACE_RENDER_TARGET = 1u << 1,
    VRO_ENGINE_SURFACE_CAMERA = 1u << 2,
    VRO_ENGINE_SURFACE_VIDEO = 1u << 3,
    VRO_ENGINE_SURFACE_EXTERNAL_PRODUCER = 1u << 4
} VROEngineSurfaceUsageBit;

typedef struct VROEngineSurfaceDesc {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t origin;
    uint32_t usage;
    uint32_t plane_count;
    uint32_t reserved0;
} VROEngineSurfaceDesc;

#define VRO_ENGINE_SURFACE_DESC_V0_1_SIZE 32u

typedef struct VROEngineSurfaceFrame {
    uint32_t struct_size;
    uint32_t flags;
    VROEngineHandle surface;
    uint64_t frame_id;
    uint64_t timestamp_ns;
    uint64_t acquire_token;
    uint64_t release_token;
} VROEngineSurfaceFrame;

#define VRO_ENGINE_SURFACE_FRAME_V0_1_SIZE 48u

VROEngineStatusCode viro_engine_surface_desc_validate(
    const VROEngineSurfaceDesc *desc);

VROEngineStatusCode viro_engine_surface_frame_validate(
    const VROEngineSurfaceFrame *frame);

/*
 * Returns true when an incoming frame ID is newer than the currently consumed
 * frame ID using monotonic unsigned ordering. Equal IDs are not newer.
 */
int32_t viro_engine_surface_frame_is_newer(
    uint64_t consumed_frame_id,
    uint64_t candidate_frame_id);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(VROEngineSurfaceDesc) == VRO_ENGINE_SURFACE_DESC_V0_1_SIZE,
              "Surface descriptor ABI v0.1 must stay 32 bytes");
static_assert(sizeof(VROEngineSurfaceFrame) == VRO_ENGINE_SURFACE_FRAME_V0_1_SIZE,
              "Surface frame ABI v0.1 must stay 48 bytes");
#endif

#endif
