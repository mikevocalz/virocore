//
// VROEngineMediaABI.cpp
//

#include "VROEngineMediaABI.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace {

std::atomic<uint64_t> gFrameCount{0};
std::atomic<uint64_t> gSurfaceFrameCount{0};
std::atomic<uint64_t> gPayloadBytes{0};
std::atomic<uint64_t> gCopyCount{0};
std::atomic<uint64_t> gCopiedBytes{0};
std::atomic<uint64_t> gMaxPayloadBytes{0};
std::atomic<uint64_t> gLastTimestampNs{0};

bool validPixelFormat(uint32_t value) {
    return value >= VRO_ENGINE_PIXEL_RGBA8_UNORM &&
           value <= VRO_ENGINE_PIXEL_DEPTH32_FLOAT;
}

bool validColorSpace(uint32_t value) {
    return value <= VRO_ENGINE_COLOR_SPACE_REC2020;
}

bool validColorRange(uint32_t value) {
    return value <= VRO_ENGINE_COLOR_RANGE_VIDEO;
}

bool validOwnership(uint32_t value) {
    return value >= VRO_ENGINE_MEDIA_BORROWED_CALL &&
           value <= VRO_ENGINE_MEDIA_BACKEND_OWNED;
}

void updateMax(std::atomic<uint64_t> &target, uint64_t value) {
    uint64_t current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(
               current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

} // namespace

extern "C" VROEngineStatusCode viro_engine_media_plane_validate(
    const VROEngineMediaPlane *plane) {
    if (plane == nullptr ||
        plane->struct_size < VRO_ENGINE_MEDIA_PLANE_V0_1_SIZE ||
        plane->width == 0 ||
        plane->height == 0 ||
        plane->row_stride_bytes == 0 ||
        plane->pixel_stride_bytes == 0 ||
        plane->bytes.data == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    const uint64_t minimumBytes =
        static_cast<uint64_t>(plane->row_stride_bytes) * plane->height;
    if (plane->bytes.length < minimumBytes) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_media_frame_validate(
    const VROEngineMediaFrame *frame) {
    if (frame == nullptr ||
        frame->struct_size < VRO_ENGINE_MEDIA_FRAME_V0_1_SIZE ||
        frame->width == 0 ||
        frame->height == 0 ||
        !validPixelFormat(frame->pixel_format) ||
        !validColorSpace(frame->color_space) ||
        !validColorRange(frame->color_range) ||
        !validOwnership(frame->ownership) ||
        frame->plane_count > 4) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (frame->ownership == VRO_ENGINE_MEDIA_BORROWED_CALL && frame->lease != 0) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    if (frame->ownership == VRO_ENGINE_MEDIA_BORROWED_LEASE && frame->lease == 0) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (frame->plane_count == 0) {
        return frame->surface != 0
            ? VRO_ENGINE_STATUS_OK
            : VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (frame->planes == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < frame->plane_count; ++i) {
        const VROEngineMediaPlane &plane = frame->planes[i];
        if (plane.plane_index != i ||
            viro_engine_media_plane_validate(&plane) != VRO_ENGINE_STATUS_OK) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
    }
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_media_frame_payload_bytes(
    const VROEngineMediaFrame *frame,
    uint64_t *out_bytes) {
    if (out_bytes == nullptr ||
        viro_engine_media_frame_validate(frame) != VRO_ENGINE_STATUS_OK) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    uint64_t total = 0;
    for (uint32_t i = 0; i < frame->plane_count; ++i) {
        const uint64_t length = frame->planes[i].bytes.length;
        if (length > std::numeric_limits<uint64_t>::max() - total) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        total += length;
    }
    *out_bytes = total;
    return VRO_ENGINE_STATUS_OK;
}

extern "C" void viro_engine_media_metrics_reset(void) {
    gFrameCount.store(0, std::memory_order_relaxed);
    gSurfaceFrameCount.store(0, std::memory_order_relaxed);
    gPayloadBytes.store(0, std::memory_order_relaxed);
    gCopyCount.store(0, std::memory_order_relaxed);
    gCopiedBytes.store(0, std::memory_order_relaxed);
    gMaxPayloadBytes.store(0, std::memory_order_relaxed);
    gLastTimestampNs.store(0, std::memory_order_relaxed);
}

extern "C" VROEngineStatusCode viro_engine_media_metrics_record(
    const VROEngineMediaFrame *frame,
    uint64_t copy_count,
    uint64_t copied_bytes) {
    uint64_t payloadBytes = 0;
    const VROEngineStatusCode status =
        viro_engine_media_frame_payload_bytes(frame, &payloadBytes);
    if (status != VRO_ENGINE_STATUS_OK) {
        return status;
    }

    gFrameCount.fetch_add(1, std::memory_order_relaxed);
    if (frame->surface != 0) {
        gSurfaceFrameCount.fetch_add(1, std::memory_order_relaxed);
    }
    gPayloadBytes.fetch_add(payloadBytes, std::memory_order_relaxed);
    gCopyCount.fetch_add(copy_count, std::memory_order_relaxed);
    gCopiedBytes.fetch_add(copied_bytes, std::memory_order_relaxed);
    gLastTimestampNs.store(frame->timestamp_ns, std::memory_order_relaxed);
    updateMax(gMaxPayloadBytes, payloadBytes);
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_media_metrics_snapshot(
    VROEngineMediaMetrics *out_metrics) {
    if (out_metrics == nullptr ||
        out_metrics->struct_size < VRO_ENGINE_MEDIA_METRICS_V0_1_SIZE) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    VROEngineMediaMetrics metrics{};
    metrics.struct_size = sizeof(metrics);
    metrics.frame_count = gFrameCount.load(std::memory_order_relaxed);
    metrics.surface_frame_count = gSurfaceFrameCount.load(std::memory_order_relaxed);
    metrics.payload_bytes = gPayloadBytes.load(std::memory_order_relaxed);
    metrics.copy_count = gCopyCount.load(std::memory_order_relaxed);
    metrics.copied_bytes = gCopiedBytes.load(std::memory_order_relaxed);
    metrics.max_payload_bytes = gMaxPayloadBytes.load(std::memory_order_relaxed);
    metrics.last_timestamp_ns = gLastTimestampNs.load(std::memory_order_relaxed);

    const size_t writeSize = std::min<size_t>(out_metrics->struct_size, sizeof(metrics));
    std::memcpy(out_metrics, &metrics, writeSize);
    return VRO_ENGINE_STATUS_OK;
}
