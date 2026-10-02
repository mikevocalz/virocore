//
// VROEngineMediaABI.h
//
// Camera/video/recording frame exchange contract.
// Supports CPU plane views, opaque GPU surfaces, or both.
//

#ifndef VRO_ENGINE_MEDIA_ABI_H
#define VRO_ENGINE_MEDIA_ABI_H

#include <stdint.h>
#include "VROEngineSurfaceABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_MEDIA_ABI_MAJOR 0u
#define VRO_ENGINE_MEDIA_ABI_MINOR 1u

typedef enum VROEngineColorSpace {
    VRO_ENGINE_COLOR_SPACE_UNKNOWN = 0,
    VRO_ENGINE_COLOR_SPACE_SRGB = 1,
    VRO_ENGINE_COLOR_SPACE_DISPLAY_P3 = 2,
    VRO_ENGINE_COLOR_SPACE_REC709 = 3,
    VRO_ENGINE_COLOR_SPACE_REC2020 = 4
} VROEngineColorSpace;

typedef enum VROEngineColorRange {
    VRO_ENGINE_COLOR_RANGE_UNKNOWN = 0,
    VRO_ENGINE_COLOR_RANGE_FULL = 1,
    VRO_ENGINE_COLOR_RANGE_VIDEO = 2
} VROEngineColorRange;

typedef enum VROEngineMediaOwnership {
    /* Plane pointers are valid only for the current call. lease must be zero. */
    VRO_ENGINE_MEDIA_BORROWED_CALL = 1,
    /* Plane pointers remain valid while the opaque lease is retained by the producer. */
    VRO_ENGINE_MEDIA_BORROWED_LEASE = 2,
    /* Backend-owned opaque surface; CPU planes may be absent. */
    VRO_ENGINE_MEDIA_BACKEND_OWNED = 3
} VROEngineMediaOwnership;

typedef struct VROEngineMediaPlane {
    uint32_t struct_size;
    uint32_t plane_index;
    uint32_t width;
    uint32_t height;
    uint32_t row_stride_bytes;
    uint32_t pixel_stride_bytes;
    uint32_t flags;
    uint32_t reserved0;
    VROEngineByteView bytes;
} VROEngineMediaPlane;

#define VRO_ENGINE_MEDIA_PLANE_V0_1_SIZE 48u

typedef struct VROEngineMediaFrame {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t color_space;
    uint32_t color_range;
    uint32_t plane_count;
    uint32_t ownership;
    uint64_t frame_id;
    uint64_t timestamp_ns;
    VROEngineHandle surface;
    VROEngineHandle lease;
    const VROEngineMediaPlane *planes;
    uint32_t flags;
    uint32_t reserved0;
} VROEngineMediaFrame;

#define VRO_ENGINE_MEDIA_FRAME_V0_1_SIZE 80u

typedef struct VROEngineMediaMetrics {
    uint32_t struct_size;
    uint32_t reserved0;
    uint64_t frame_count;
    uint64_t surface_frame_count;
    uint64_t payload_bytes;
    uint64_t copy_count;
    uint64_t copied_bytes;
    uint64_t max_payload_bytes;
    uint64_t last_timestamp_ns;
} VROEngineMediaMetrics;

#define VRO_ENGINE_MEDIA_METRICS_V0_1_SIZE 64u

VROEngineStatusCode viro_engine_media_plane_validate(
    const VROEngineMediaPlane *plane);

VROEngineStatusCode viro_engine_media_frame_validate(
    const VROEngineMediaFrame *frame);

VROEngineStatusCode viro_engine_media_frame_payload_bytes(
    const VROEngineMediaFrame *frame,
    uint64_t *out_bytes);

void viro_engine_media_metrics_reset(void);

/*
 * copy_count/copied_bytes are supplied by the adapter because only the producer
 * knows whether platform conversion/upload caused a copy.
 */
VROEngineStatusCode viro_engine_media_metrics_record(
    const VROEngineMediaFrame *frame,
    uint64_t copy_count,
    uint64_t copied_bytes);

VROEngineStatusCode viro_engine_media_metrics_snapshot(
    VROEngineMediaMetrics *out_metrics);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(VROEngineMediaPlane) == VRO_ENGINE_MEDIA_PLANE_V0_1_SIZE,
              "Media plane ABI v0.1 must stay 48 bytes");
static_assert(sizeof(VROEngineMediaFrame) == VRO_ENGINE_MEDIA_FRAME_V0_1_SIZE,
              "Media frame ABI v0.1 must stay 80 bytes");
static_assert(sizeof(VROEngineMediaMetrics) == VRO_ENGINE_MEDIA_METRICS_V0_1_SIZE,
              "Media metrics ABI v0.1 must stay 64 bytes");
#endif

#endif // VRO_ENGINE_MEDIA_ABI_H
