#ifndef VRO_ENGINE_WORLD_MESH_BACKEND_H
#define VRO_ENGINE_WORLD_MESH_BACKEND_H

#include <stdint.h>

#include "VROEngineBackendSelector.h"
#include "VROEngineWorldMeshABI.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t viro_engine_world_mesh_backend_availability(void);

VROEngineBackendKind viro_engine_world_mesh_backend_resolve(void);

VROEngineStatusCode viro_engine_world_mesh_chunk_validate_selected(
    const VROEngineWorldMeshChunk *chunk);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VRO_ENGINE_WORLD_MESH_BACKEND_H
