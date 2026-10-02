#include "VROEngineWorldMeshBackend.h"

#if defined(VRO_ENGINE_ESKIU_WORLD_MESH_AVAILABLE) && VRO_ENGINE_ESKIU_WORLD_MESH_AVAILABLE
extern "C" {
int viro_eskiu_world_mesh_chunk_validate(VROEngineWorldMeshChunk *chunk);
}
#endif

extern "C" uint32_t viro_engine_world_mesh_backend_availability(void) {
    uint32_t available = VRO_ENGINE_BACKEND_AVAILABLE_CPP;
#if defined(VRO_ENGINE_ESKIU_WORLD_MESH_AVAILABLE) && VRO_ENGINE_ESKIU_WORLD_MESH_AVAILABLE
    available |= VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;
#endif
    return available;
}

extern "C" VROEngineBackendKind viro_engine_world_mesh_backend_resolve(void) {
    return viro_engine_backend_resolve(
        VRO_ENGINE_DOMAIN_WORLD_MESH,
        viro_engine_world_mesh_backend_availability());
}

extern "C" VROEngineStatusCode viro_engine_world_mesh_chunk_validate_selected(
    const VROEngineWorldMeshChunk *chunk) {
    const VROEngineBackendKind backend =
        viro_engine_world_mesh_backend_resolve();

    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_world_mesh_chunk_validate(chunk);
    }

#if defined(VRO_ENGINE_ESKIU_WORLD_MESH_AVAILABLE) && VRO_ENGINE_ESKIU_WORLD_MESH_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_world_mesh_chunk_validate(
                const_cast<VROEngineWorldMeshChunk *>(chunk)));
    }
#endif

    return VRO_ENGINE_STATUS_UNSUPPORTED;
}
