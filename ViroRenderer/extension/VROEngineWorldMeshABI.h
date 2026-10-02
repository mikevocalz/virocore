//
// VROEngineWorldMeshABI.h
//
// Chunked world-mesh exchange ABI built on the generic geometry contract.
// The ABI describes borrowed mesh payloads; it does not own or copy them.
//

#ifndef VRO_ENGINE_WORLD_MESH_ABI_H
#define VRO_ENGINE_WORLD_MESH_ABI_H

#include <stdint.h>
#include "VROEngineGeometryABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_WORLD_MESH_ABI_MAJOR 0u
#define VRO_ENGINE_WORLD_MESH_ABI_MINOR 1u

typedef enum VROEngineWorldMeshSource {
    VRO_ENGINE_WORLD_MESH_SOURCE_UNKNOWN = 0,
    VRO_ENGINE_WORLD_MESH_SOURCE_LIDAR = 1,
    VRO_ENGINE_WORLD_MESH_SOURCE_MONOCULAR = 2,
    VRO_ENGINE_WORLD_MESH_SOURCE_PLANE = 3
} VROEngineWorldMeshSource;

typedef enum VROEngineWorldMeshUpdateKind {
    VRO_ENGINE_WORLD_MESH_UPSERT = 1,
    VRO_ENGINE_WORLD_MESH_REMOVE = 2
} VROEngineWorldMeshUpdateKind;

typedef struct VROEngineWorldMeshChunk {
    uint32_t struct_size;
    uint32_t update_kind;
    uint64_t chunk_id;
    uint64_t version;
    uint64_t timestamp_ns;
    uint32_t source;
    uint32_t flags;

    /*
     * Borrowed geometry. For UPSERT this must be a triangle geometry descriptor.
     * For REMOVE geometry.struct_size must be zero and all payload views must be empty.
     */
    VROEngineGeometryDesc geometry;

    /*
     * Optional per-vertex float32 confidence values. Empty is valid.
     * Lifetime is identical to geometry.vertices/indices: call scoped unless the
     * producer and consumer explicitly negotiate a longer lifetime elsewhere.
     */
    VROEngineByteView confidences;
} VROEngineWorldMeshChunk;

#define VRO_ENGINE_WORLD_MESH_CHUNK_V0_1_SIZE 144u

typedef struct VROEngineWorldMeshMetrics {
    uint32_t struct_size;
    uint32_t reserved0;
    uint64_t update_count;
    uint64_t remove_count;
    uint64_t vertex_bytes;
    uint64_t index_bytes;
    uint64_t confidence_bytes;
    uint64_t max_chunk_payload_bytes;
    uint64_t last_timestamp_ns;
} VROEngineWorldMeshMetrics;

#define VRO_ENGINE_WORLD_MESH_METRICS_V0_1_SIZE 64u

VROEngineStatusCode viro_engine_world_mesh_chunk_validate(
    const VROEngineWorldMeshChunk *chunk);

/*
 * Instrumentation is opt-in at call sites. The counters are cheap atomics and
 * measure payload volume/high-water mark, not whole-process RSS.
 */
void viro_engine_world_mesh_metrics_reset(void);
VROEngineStatusCode viro_engine_world_mesh_metrics_record(
    const VROEngineWorldMeshChunk *chunk);
VROEngineStatusCode viro_engine_world_mesh_metrics_snapshot(
    VROEngineWorldMeshMetrics *out_metrics);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(VROEngineWorldMeshChunk) ==
                  VRO_ENGINE_WORLD_MESH_CHUNK_V0_1_SIZE,
              "World mesh chunk ABI v0.1 must stay 144 bytes");
static_assert(sizeof(VROEngineWorldMeshMetrics) ==
                  VRO_ENGINE_WORLD_MESH_METRICS_V0_1_SIZE,
              "World mesh metrics ABI v0.1 must stay 64 bytes");
#endif

#endif // VRO_ENGINE_WORLD_MESH_ABI_H
