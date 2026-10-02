#include "VROEngineWorldMeshBackend.h"

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
    VROEngineVertexAttribute attr{
        VRO_ENGINE_VERTEX_POSITION,
        VRO_ENGINE_COMPONENT_FLOAT32,
        3,
        0
    };
    float vertices[9] = {
        0,0,0,
        1,0,0,
        0,1,0
    };
    uint32_t indices[3] = {0,1,2};
    float confidences[3] = {1.0f, 0.8f, 0.9f};
    VROEngineWorldMeshChunk chunk{};

    Fixture() {
        chunk.struct_size = sizeof(chunk);
        chunk.update_kind = VRO_ENGINE_WORLD_MESH_UPSERT;
        chunk.chunk_id = 7;
        chunk.version = 1;
        chunk.timestamp_ns = 100;
        chunk.source = VRO_ENGINE_WORLD_MESH_SOURCE_LIDAR;

        chunk.geometry.struct_size = sizeof(VROEngineGeometryDesc);
        chunk.geometry.topology = VRO_ENGINE_TOPOLOGY_TRIANGLES;
        chunk.geometry.vertex_stride_bytes = sizeof(float) * 3;
        chunk.geometry.vertex_count = 3;
        chunk.geometry.index_type = VRO_ENGINE_INDEX_UINT32;
        chunk.geometry.index_count = 3;
        chunk.geometry.attribute_count = 1;
        chunk.geometry.attributes = &attr;
        chunk.geometry.vertices = {
            reinterpret_cast<const uint8_t *>(vertices), sizeof(vertices)
        };
        chunk.geometry.indices = {
            reinterpret_cast<const uint8_t *>(indices), sizeof(indices)
        };
        chunk.geometry.geometry_id = 7;
        chunk.geometry.version = 1;
        chunk.confidences = {
            reinterpret_cast<const uint8_t *>(confidences),
            sizeof(confidences)
        };
    }
};

bool runBackend(
    VROEngineBackendPreference preference,
    VROEngineBackendKind expectedBackend) {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_WORLD_MESH, preference) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    bool ok = true;
    ok &= expect(
        viro_engine_world_mesh_backend_resolve() == expectedBackend,
        "resolved backend");

    Fixture fixture;
    ok &= expect(
        viro_engine_world_mesh_chunk_validate_selected(&fixture.chunk) ==
            VRO_ENGINE_STATUS_OK,
        "valid upsert");

    fixture.chunk.confidences.length = sizeof(float);
    ok &= expect(
        viro_engine_world_mesh_chunk_validate_selected(&fixture.chunk) ==
            VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "short confidence rejected");

    VROEngineWorldMeshChunk remove{};
    remove.struct_size = sizeof(remove);
    remove.update_kind = VRO_ENGINE_WORLD_MESH_REMOVE;
    remove.chunk_id = 7;
    remove.version = 2;
    remove.timestamp_ns = 101;
    remove.source = VRO_ENGINE_WORLD_MESH_SOURCE_LIDAR;
    ok &= expect(
        viro_engine_world_mesh_chunk_validate_selected(&remove) ==
            VRO_ENGINE_STATUS_OK,
        "valid remove");

    remove.confidences = {
        reinterpret_cast<const uint8_t *>(fixture.confidences),
        sizeof(float)
    };
    ok &= expect(
        viro_engine_world_mesh_chunk_validate_selected(&remove) ==
            VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "remove payload rejected");

    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= expect(
        (viro_engine_world_mesh_backend_availability() &
         VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) != 0,
        "Eskiu world mesh backend compiled");
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
    std::cout << "Eskiu world mesh runtime backend passed\n";
    return EXIT_SUCCESS;
}
