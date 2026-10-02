#include "VROEngineMediaBackend.h"
#include "VROEngineSurfaceBackend.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

bool expect(bool ok, const char *label) {
    if (!ok) {
        std::cerr << "FAIL: " << label << "\n";
        return false;
    }
    return true;
}

bool runSurface(
    VROEngineBackendPreference preference,
    VROEngineBackendKind expectedBackend) {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_SURFACE, preference) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    bool ok = expect(
        viro_engine_surface_backend_resolve() == expectedBackend,
        "surface backend");

    VROEngineSurfaceDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = 1920;
    desc.height = 1080;
    desc.pixel_format = VRO_ENGINE_PIXEL_BGRA8_UNORM;
    desc.origin = VRO_ENGINE_SURFACE_ORIGIN_TOP_LEFT;
    desc.usage = VRO_ENGINE_SURFACE_SAMPLED;
    desc.plane_count = 1;
    ok &= expect(
        viro_engine_surface_desc_validate_selected(&desc) ==
            VRO_ENGINE_STATUS_OK,
        "surface desc valid");

    desc.width = 0;
    ok &= expect(
        viro_engine_surface_desc_validate_selected(&desc) ==
            VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "surface desc invalid");

    VROEngineSurfaceFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.surface = 42;
    frame.frame_id = 7;
    ok &= expect(
        viro_engine_surface_frame_validate_selected(&frame) ==
            VRO_ENGINE_STATUS_OK,
        "surface frame valid");
    ok &= expect(
        viro_engine_surface_frame_is_newer_selected(7, 8) == 1 &&
        viro_engine_surface_frame_is_newer_selected(8, 8) == 0,
        "surface ordering");
    return ok;
}

bool runMedia(
    VROEngineBackendPreference preference,
    VROEngineBackendKind expectedBackend) {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_MEDIA, preference) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    bool ok = expect(
        viro_engine_media_backend_resolve() == expectedBackend,
        "media backend");

    std::vector<uint8_t> rgba(64, 0x7f);
    VROEngineMediaPlane plane{};
    plane.struct_size = sizeof(plane);
    plane.plane_index = 0;
    plane.width = 4;
    plane.height = 4;
    plane.row_stride_bytes = 16;
    plane.pixel_stride_bytes = 4;
    plane.bytes = {rgba.data(), rgba.size()};

    ok &= expect(
        viro_engine_media_plane_validate_selected(&plane) ==
            VRO_ENGINE_STATUS_OK,
        "media plane valid");

    VROEngineMediaFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.width = 4;
    frame.height = 4;
    frame.pixel_format = VRO_ENGINE_PIXEL_RGBA8_UNORM;
    frame.color_space = VRO_ENGINE_COLOR_SPACE_SRGB;
    frame.color_range = VRO_ENGINE_COLOR_RANGE_FULL;
    frame.plane_count = 1;
    frame.ownership = VRO_ENGINE_MEDIA_BORROWED_CALL;
    frame.planes = &plane;

    ok &= expect(
        viro_engine_media_frame_validate_selected(&frame) ==
            VRO_ENGINE_STATUS_OK,
        "media frame valid");

    uint64_t payload = 0;
    ok &= expect(
        viro_engine_media_frame_payload_bytes_selected(&frame, &payload) ==
            VRO_ENGINE_STATUS_OK &&
        payload == rgba.size(),
        "media payload");

    frame.ownership = VRO_ENGINE_MEDIA_BORROWED_LEASE;
    frame.lease = 0;
    ok &= expect(
        viro_engine_media_frame_validate_selected(&frame) ==
            VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "media lease invalid");
    return ok;
}

bool runPreference(
    VROEngineBackendPreference preference,
    VROEngineBackendKind expectedBackend) {
    return runSurface(preference, expectedBackend) &&
           runMedia(preference, expectedBackend);
}

} // namespace

int main() {
    bool ok = true;
    ok &= expect(
        (viro_engine_surface_backend_availability() &
         VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) != 0,
        "Eskiu surface compiled");
    ok &= expect(
        (viro_engine_media_backend_availability() &
         VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) != 0,
        "Eskiu media compiled");
    ok &= expect(
        runPreference(VRO_ENGINE_BACKEND_PREFERENCE_AUTO,
                      VRO_ENGINE_BACKEND_CPP),
        "AUTO remains C++");
    ok &= expect(
        runPreference(VRO_ENGINE_BACKEND_PREFERENCE_CPP,
                      VRO_ENGINE_BACKEND_CPP),
        "explicit C++");
    ok &= expect(
        runPreference(VRO_ENGINE_BACKEND_PREFERENCE_ESKIU,
                      VRO_ENGINE_BACKEND_ESKIU),
        "explicit Eskiu");

    viro_engine_backend_preferences_reset();
    if (!ok) return EXIT_FAILURE;
    std::cout << "Eskiu surface/media runtime backends passed\n";
    return EXIT_SUCCESS;
}
