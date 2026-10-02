//
// VROEngineWorldMeshABI.cpp
//

#include "VROEngineWorldMeshABI.h"
#include "VROEngineGeometryBackend.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace {

std::atomic<uint64_t> gUpdateCount{0};
std::atomic<uint64_t> gRemoveCount{0};
std::atomic<uint64_t> gVertexBytes{0};
std::atomic<uint64_t> gIndexBytes{0};
std::atomic<uint64_t> gConfidenceBytes{0};
std::atomic<uint64_t> gMaxChunkPayloadBytes{0};
std::atomic<uint64_t> gLastTimestampNs{0};

bool validSource(uint32_t source) {
    return source <= VRO_ENGINE_WORLD_MESH_SOURCE_PLANE;
}

void updateMax(std::atomic<uint64_t> &target, uint64_t value) {
    uint64_t current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(
               current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

} // namespace

extern "C" VROEngineStatusCode viro_engine_world_mesh_chunk_validate(
    const VROEngineWorldMeshChunk *chunk) {
    if (chunk == nullptr ||
        chunk->struct_size < VRO_ENGINE_WORLD_MESH_CHUNK_V0_1_SIZE ||
        chunk->chunk_id == 0 ||
        chunk->version == 0 ||
        !validSource(chunk->source)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->update_kind == VRO_ENGINE_WORLD_MESH_REMOVE) {
        if (chunk->geometry.struct_size != 0 ||
            chunk->confidences.data != nullptr ||
            chunk->confidences.length != 0) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        return VRO_ENGINE_STATUS_OK;
    }

    if (chunk->update_kind != VRO_ENGINE_WORLD_MESH_UPSERT ||
        chunk->geometry.topology != VRO_ENGINE_TOPOLOGY_TRIANGLES ||
        chunk->geometry.index_type == VRO_ENGINE_INDEX_NONE ||
        (chunk->geometry.index_count % 3u) != 0u ||
        viro_engine_geometry_validate_selected(&chunk->geometry) != VRO_ENGINE_STATUS_OK) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    const uint64_t confidenceBytes =
        static_cast<uint64_t>(chunk->geometry.vertex_count) * sizeof(float);
    if (chunk->confidences.length != 0) {
        if (chunk->confidences.data == nullptr ||
            chunk->confidences.length < confidenceBytes) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
    }
    return VRO_ENGINE_STATUS_OK;
}

extern "C" void viro_engine_world_mesh_metrics_reset(void) {
    gUpdateCount.store(0, std::memory_order_relaxed);
    gRemoveCount.store(0, std::memory_order_relaxed);
    gVertexBytes.store(0, std::memory_order_relaxed);
    gIndexBytes.store(0, std::memory_order_relaxed);
    gConfidenceBytes.store(0, std::memory_order_relaxed);
    gMaxChunkPayloadBytes.store(0, std::memory_order_relaxed);
    gLastTimestampNs.store(0, std::memory_order_relaxed);
}

extern "C" VROEngineStatusCode viro_engine_world_mesh_metrics_record(
    const VROEngineWorldMeshChunk *chunk) {
    const VROEngineStatusCode status = viro_engine_world_mesh_chunk_validate(chunk);
    if (status != VRO_ENGINE_STATUS_OK) {
        return status;
    }

    gLastTimestampNs.store(chunk->timestamp_ns, std::memory_order_relaxed);
    if (chunk->update_kind == VRO_ENGINE_WORLD_MESH_REMOVE) {
        gRemoveCount.fetch_add(1, std::memory_order_relaxed);
        return VRO_ENGINE_STATUS_OK;
    }

    const uint64_t vertexBytes = chunk->geometry.vertices.length;
    const uint64_t indexBytes = chunk->geometry.indices.length;
    const uint64_t confidenceBytes = chunk->confidences.length;
    const uint64_t payloadBytes = vertexBytes + indexBytes + confidenceBytes;

    gUpdateCount.fetch_add(1, std::memory_order_relaxed);
    gVertexBytes.fetch_add(vertexBytes, std::memory_order_relaxed);
    gIndexBytes.fetch_add(indexBytes, std::memory_order_relaxed);
    gConfidenceBytes.fetch_add(confidenceBytes, std::memory_order_relaxed);
    updateMax(gMaxChunkPayloadBytes, payloadBytes);
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_world_mesh_metrics_snapshot(
    VROEngineWorldMeshMetrics *out_metrics) {
    if (out_metrics == nullptr ||
        out_metrics->struct_size < VRO_ENGINE_WORLD_MESH_METRICS_V0_1_SIZE) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    VROEngineWorldMeshMetrics metrics{};
    metrics.struct_size = sizeof(metrics);
    metrics.update_count = gUpdateCount.load(std::memory_order_relaxed);
    metrics.remove_count = gRemoveCount.load(std::memory_order_relaxed);
    metrics.vertex_bytes = gVertexBytes.load(std::memory_order_relaxed);
    metrics.index_bytes = gIndexBytes.load(std::memory_order_relaxed);
    metrics.confidence_bytes = gConfidenceBytes.load(std::memory_order_relaxed);
    metrics.max_chunk_payload_bytes =
        gMaxChunkPayloadBytes.load(std::memory_order_relaxed);
    metrics.last_timestamp_ns = gLastTimestampNs.load(std::memory_order_relaxed);

    const size_t writeSize = std::min<size_t>(out_metrics->struct_size, sizeof(metrics));
    std::memcpy(out_metrics, &metrics, writeSize);
    return VRO_ENGINE_STATUS_OK;
}
