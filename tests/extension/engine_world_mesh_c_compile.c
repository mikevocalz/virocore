#include "VROEngineWorldMeshABI.h"

_Static_assert(sizeof(VROEngineWorldMeshChunk) ==
                   VRO_ENGINE_WORLD_MESH_CHUNK_V0_1_SIZE,
               "world mesh chunk C ABI changed");
_Static_assert(sizeof(VROEngineWorldMeshMetrics) ==
                   VRO_ENGINE_WORLD_MESH_METRICS_V0_1_SIZE,
               "world mesh metrics C ABI changed");

int viro_world_mesh_c_compile_probe(void) {
    VROEngineWorldMeshChunk chunk = {0};
    chunk.struct_size = sizeof(chunk);
    return (int)chunk.struct_size;
}
