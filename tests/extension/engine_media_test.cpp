#include "VROEngineMediaABI.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<uint8_t> rgba(4 * 4 * 4, 0x7f);

    VROEngineMediaPlane plane{};
    plane.struct_size = sizeof(plane);
    plane.plane_index = 0;
    plane.width = 4;
    plane.height = 4;
    plane.row_stride_bytes = 16;
    plane.pixel_stride_bytes = 4;
    plane.bytes = {rgba.data(), rgba.size()};
    assert(viro_engine_media_plane_validate(&plane) == VRO_ENGINE_STATUS_OK);

    VROEngineMediaFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.width = 4;
    frame.height = 4;
    frame.pixel_format = VRO_ENGINE_PIXEL_RGBA8_UNORM;
    frame.color_space = VRO_ENGINE_COLOR_SPACE_SRGB;
    frame.color_range = VRO_ENGINE_COLOR_RANGE_FULL;
    frame.plane_count = 1;
    frame.ownership = VRO_ENGINE_MEDIA_BORROWED_CALL;
    frame.frame_id = 5;
    frame.timestamp_ns = 900;
    frame.planes = &plane;
    assert(viro_engine_media_frame_validate(&frame) == VRO_ENGINE_STATUS_OK);

    uint64_t bytes = 0;
    assert(viro_engine_media_frame_payload_bytes(&frame, &bytes) ==
           VRO_ENGINE_STATUS_OK);
    assert(bytes == rgba.size());

    viro_engine_media_metrics_reset();
    assert(viro_engine_media_metrics_record(&frame, 1, bytes) ==
           VRO_ENGINE_STATUS_OK);

    VROEngineMediaMetrics metrics{};
    metrics.struct_size = sizeof(metrics);
    assert(viro_engine_media_metrics_snapshot(&metrics) == VRO_ENGINE_STATUS_OK);
    assert(metrics.frame_count == 1);
    assert(metrics.surface_frame_count == 0);
    assert(metrics.payload_bytes == bytes);
    assert(metrics.copy_count == 1);
    assert(metrics.copied_bytes == bytes);
    assert(metrics.max_payload_bytes == bytes);
    assert(metrics.last_timestamp_ns == 900);

    VROEngineMediaFrame gpu{};
    gpu.struct_size = sizeof(gpu);
    gpu.width = 1920;
    gpu.height = 1080;
    gpu.pixel_format = VRO_ENGINE_PIXEL_BGRA8_UNORM;
    gpu.color_space = VRO_ENGINE_COLOR_SPACE_REC709;
    gpu.color_range = VRO_ENGINE_COLOR_RANGE_VIDEO;
    gpu.plane_count = 0;
    gpu.ownership = VRO_ENGINE_MEDIA_BACKEND_OWNED;
    gpu.frame_id = 6;
    gpu.timestamp_ns = 1000;
    gpu.surface = 99;
    assert(viro_engine_media_frame_validate(&gpu) == VRO_ENGINE_STATUS_OK);
    assert(viro_engine_media_metrics_record(&gpu, 0, 0) == VRO_ENGINE_STATUS_OK);

    metrics.struct_size = sizeof(metrics);
    assert(viro_engine_media_metrics_snapshot(&metrics) == VRO_ENGINE_STATUS_OK);
    assert(metrics.frame_count == 2);
    assert(metrics.surface_frame_count == 1);

    frame.ownership = VRO_ENGINE_MEDIA_BORROWED_LEASE;
    frame.lease = 0;
    assert(viro_engine_media_frame_validate(&frame) ==
           VRO_ENGINE_STATUS_INVALID_ARGUMENT);

    std::cout << "Engine media ABI: PASS\n";
    return 0;
}
