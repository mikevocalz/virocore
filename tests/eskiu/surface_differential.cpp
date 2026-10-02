#include "VROEngineSurfaceABI.h"
#include <cstdlib>
#include <iostream>

extern "C" {
int viro_eskiu_surface_desc_validate(VROEngineSurfaceDesc *desc);
int viro_eskiu_surface_frame_validate(VROEngineSurfaceFrame *frame);
int viro_eskiu_surface_frame_is_newer(uint64_t consumed, uint64_t candidate);
}

static void expect(bool ok, const char *label) {
    if (!ok) { std::cerr << "FAIL " << label << "\n"; std::exit(EXIT_FAILURE); }
}

int main() {
    VROEngineSurfaceDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = 1920; desc.height = 1080;
    desc.pixel_format = VRO_ENGINE_PIXEL_BGRA8_UNORM;
    desc.origin = VRO_ENGINE_SURFACE_ORIGIN_TOP_LEFT;
    desc.usage = VRO_ENGINE_SURFACE_SAMPLED;
    desc.plane_count = 1;

    auto checkDesc = [&](const char *label) {
        expect((int)viro_engine_surface_desc_validate(&desc) ==
               viro_eskiu_surface_desc_validate(&desc), label);
    };
    checkDesc("valid desc");
    desc.width = 0; checkDesc("zero width"); desc.width = 1920;
    desc.pixel_format = 99; checkDesc("bad format"); desc.pixel_format = VRO_ENGINE_PIXEL_BGRA8_UNORM;
    desc.origin = 99; checkDesc("bad origin"); desc.origin = VRO_ENGINE_SURFACE_ORIGIN_TOP_LEFT;
    desc.plane_count = 0; checkDesc("zero planes"); desc.plane_count = 1;

    VROEngineSurfaceFrame frame{};
    frame.struct_size = sizeof(frame); frame.surface = 42; frame.frame_id = 7;
    auto checkFrame = [&](const char *label) {
        expect((int)viro_engine_surface_frame_validate(&frame) ==
               viro_eskiu_surface_frame_validate(&frame), label);
    };
    checkFrame("valid frame");
    frame.surface = 0; checkFrame("zero surface"); frame.surface = 42;

    for (uint64_t a : {0ull, 7ull, 8ull}) {
        for (uint64_t b : {0ull, 7ull, 8ull}) {
            expect(viro_engine_surface_frame_is_newer(a,b) ==
                   viro_eskiu_surface_frame_is_newer(a,b), "frame ordering");
        }
    }
    std::cout << "C++ / Eskiu surface differential validation: PASS\n";
}
