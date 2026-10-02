#include "VROEngineSurfaceABI.h"

#include <cstdlib>
#include <iostream>

namespace {
bool expect(bool c, const char *m) {
    if (!c) std::cerr << "FAIL: " << m << "\n";
    return c;
}
}

int main() {
    bool ok = true;

    VROEngineSurfaceDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = 1920;
    desc.height = 1080;
    desc.pixel_format = VRO_ENGINE_PIXEL_BGRA8_UNORM;
    desc.origin = VRO_ENGINE_SURFACE_ORIGIN_TOP_LEFT;
    desc.usage = VRO_ENGINE_SURFACE_SAMPLED |
                 VRO_ENGINE_SURFACE_EXTERNAL_PRODUCER;
    desc.plane_count = 1;
    ok &= expect(viro_engine_surface_desc_validate(&desc) == VRO_ENGINE_STATUS_OK,
                 "valid surface desc");

    desc.width = 0;
    ok &= expect(viro_engine_surface_desc_validate(&desc) ==
                     VRO_ENGINE_STATUS_INVALID_ARGUMENT,
                 "zero width rejected");
    desc.width = 1920;

    VROEngineSurfaceFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.surface = 42;
    frame.frame_id = 7;
    frame.timestamp_ns = 100;
    frame.acquire_token = 10;
    frame.release_token = 11;
    ok &= expect(viro_engine_surface_frame_validate(&frame) == VRO_ENGINE_STATUS_OK,
                 "valid frame");

    frame.surface = 0;
    ok &= expect(viro_engine_surface_frame_validate(&frame) ==
                     VRO_ENGINE_STATUS_INVALID_ARGUMENT,
                 "zero handle rejected");

    ok &= expect(viro_engine_surface_frame_is_newer(7, 8) == 1,
                 "new frame");
    ok &= expect(viro_engine_surface_frame_is_newer(7, 7) == 0,
                 "same frame");
    ok &= expect(viro_engine_surface_frame_is_newer(8, 7) == 0,
                 "older frame");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
