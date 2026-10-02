#include "VROEngineWorldMeshABI.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    VROEngineVertexAttribute attr{
        VRO_ENGINE_VERTEX_POSITION,
        VRO_ENGINE_COMPONENT_FLOAT32,
        3,
        0
    };
    float vertices[] = {
        0, 0, 0,
        1, 0, 0,
        0, 1, 0
    };
    uint32_t indices[] = {0, 1, 2};
    float confidence[] = {1.0f, 0.8f, 0.9f};

    VROEngineWorldMeshChunk chunk{};
    chunk.struct_size = sizeof(chunk);
    chunk.update_kind = VRO_ENGINE_WORLD_MESH_UPSERT;
    chunk.chunk_id = 7;
    chunk.version = 1;
    chunk.timestamp_ns = 1234;
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
        reinterpret_cast<const uint8_t *>(vertices),
        sizeof(vertices)
    };
    chunk.geometry.indices = {
        reinterpret_cast<const uint8_t *>(indices),
        sizeof(indices)
    };
    chunk.geometry.geometry_id = 7;
    chunk.geometry.version = 1;
    chunk.confidences = {
        reinterpret_cast<const uint8_t *>(confidence),
        sizeof(confidence)
    };

    assert(viro_engine_world_mesh_chunk_validate(&chunk) == VRO_ENGINE_STATUS_OK);

    viro_engine_world_mesh_metrics_reset();
    assert(viro_engine_world_mesh_metrics_record(&chunk) == VRO_ENGINE_STATUS_OK);

    VROEngineWorldMeshMetrics metrics{};
    metrics.struct_size = sizeof(metrics);
    assert(viro_engine_world_mesh_metrics_snapshot(&metrics) == VRO_ENGINE_STATUS_OK);
    assert(metrics.update_count == 1);
    assert(metrics.remove_count == 0);
    assert(metrics.vertex_bytes == sizeof(vertices));
    assert(metrics.index_bytes == sizeof(indices));
    assert(metrics.confidence_bytes == sizeof(confidence));
    assert(metrics.max_chunk_payload_bytes ==
           sizeof(vertices) + sizeof(indices) + sizeof(confidence));
    assert(metrics.last_timestamp_ns == 1234);

    VROEngineWorldMeshChunk remove{};
    remove.struct_size = sizeof(remove);
    remove.update_kind = VRO_ENGINE_WORLD_MESH_REMOVE;
    remove.chunk_id = 7;
    remove.version = 2;
    remove.timestamp_ns = 2000;
    remove.source = VRO_ENGINE_WORLD_MESH_SOURCE_LIDAR;
    assert(viro_engine_world_mesh_metrics_record(&remove) == VRO_ENGINE_STATUS_OK);

    metrics.struct_size = sizeof(metrics);
    assert(viro_engine_world_mesh_metrics_snapshot(&metrics) == VRO_ENGINE_STATUS_OK);
    assert(metrics.update_count == 1);
    assert(metrics.remove_count == 1);
    assert(metrics.last_timestamp_ns == 2000);

    chunk.confidences.length = sizeof(float);
    assert(viro_engine_world_mesh_chunk_validate(&chunk) ==
           VRO_ENGINE_STATUS_INVALID_ARGUMENT);

    std::cout << "Engine world mesh ABI: PASS\n";
    return 0;
}
