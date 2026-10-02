#include "VROEngineGeometryBackend.h"

#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

struct Fixture {
    VROEngineVertexAttribute attrs[2] = {
        {VRO_ENGINE_VERTEX_POSITION, VRO_ENGINE_COMPONENT_FLOAT32, 3, 0},
        {VRO_ENGINE_VERTEX_NORMAL, VRO_ENGINE_COMPONENT_FLOAT32, 3, 12},
    };
    float vertices[18] = {
        0,0,0, 0,0,1,
        1,0,0, 0,0,1,
        0,1,0, 0,0,1,
    };
    uint32_t indices[3] = {0,1,2};
    VROEngineGeometryDesc desc{};

    Fixture() {
        desc.struct_size = sizeof(desc);
        desc.topology = VRO_ENGINE_TOPOLOGY_TRIANGLES;
        desc.vertex_stride_bytes = sizeof(float) * 6;
        desc.vertex_count = 3;
        desc.index_type = VRO_ENGINE_INDEX_UINT32;
        desc.index_count = 3;
        desc.attribute_count = 2;
        desc.attributes = attrs;
        desc.vertices = {
            reinterpret_cast<const uint8_t *>(vertices), sizeof(vertices)
        };
        desc.indices = {
            reinterpret_cast<const uint8_t *>(indices), sizeof(indices)
        };
        desc.geometry_id = 42;
        desc.version = 7;
    }
};

bool runBackend(
    VROEngineBackendPreference preference,
    VROEngineBackendKind expectedBackend) {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_GEOMETRY, preference) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    Fixture fixture;
    bool ok = true;
    ok &= expect(
        viro_engine_geometry_backend_resolve() == expectedBackend,
        "resolved backend");
    ok &= expect(
        viro_engine_geometry_validate_selected(&fixture.desc) ==
            VRO_ENGINE_STATUS_OK,
        "valid descriptor");

    auto bad = fixture.desc;
    bad.topology = 99;
    ok &= expect(
        viro_engine_geometry_validate_selected(&bad) ==
            VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "invalid descriptor");

    VROEngineGeometryRangeUpdate update{};
    update.struct_size = sizeof(update);
    update.stream_kind = 0;
    update.geometry_id = fixture.desc.geometry_id;
    update.base_version = fixture.desc.version;
    update.next_version = fixture.desc.version + 1;
    update.offset_bytes = 0;
    update.bytes = {
        reinterpret_cast<const uint8_t *>(fixture.vertices),
        sizeof(float) * 6
    };
    ok &= expect(
        viro_engine_geometry_range_validate_selected(
            &fixture.desc, &update) == VRO_ENGINE_STATUS_OK,
        "valid range");

    update.next_version = update.base_version;
    ok &= expect(
        viro_engine_geometry_range_validate_selected(
            &fixture.desc, &update) == VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "invalid range");

    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= expect(
        (viro_engine_geometry_backend_availability() &
         VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) != 0,
        "Eskiu geometry backend compiled");
    ok &= expect(
        runBackend(
            VRO_ENGINE_BACKEND_PREFERENCE_AUTO,
            VRO_ENGINE_BACKEND_CPP),
        "AUTO remains C++");
    ok &= expect(
        runBackend(
            VRO_ENGINE_BACKEND_PREFERENCE_CPP,
            VRO_ENGINE_BACKEND_CPP),
        "explicit C++");
    ok &= expect(
        runBackend(
            VRO_ENGINE_BACKEND_PREFERENCE_ESKIU,
            VRO_ENGINE_BACKEND_ESKIU),
        "explicit Eskiu");

    viro_engine_backend_preferences_reset();
    if (!ok) {
        return EXIT_FAILURE;
    }
    std::cout << "Eskiu geometry runtime backend passed\n";
    return EXIT_SUCCESS;
}
