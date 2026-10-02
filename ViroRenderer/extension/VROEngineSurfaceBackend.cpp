#include "VROEngineSurfaceBackend.h"

#if defined(VRO_ENGINE_ESKIU_SURFACE_AVAILABLE) && VRO_ENGINE_ESKIU_SURFACE_AVAILABLE
extern "C" {
int viro_eskiu_surface_desc_validate(VROEngineSurfaceDesc *desc);
int viro_eskiu_surface_frame_validate(VROEngineSurfaceFrame *frame);
int viro_eskiu_surface_frame_is_newer(
    uint64_t consumed_frame_id,
    uint64_t candidate_frame_id);
}
#endif

extern "C" uint32_t viro_engine_surface_backend_availability(void) {
    uint32_t available = VRO_ENGINE_BACKEND_AVAILABLE_CPP;
#if defined(VRO_ENGINE_ESKIU_SURFACE_AVAILABLE) && VRO_ENGINE_ESKIU_SURFACE_AVAILABLE
    available |= VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;
#endif
    return available;
}

extern "C" VROEngineBackendKind viro_engine_surface_backend_resolve(void) {
    return viro_engine_backend_resolve(
        VRO_ENGINE_DOMAIN_SURFACE,
        viro_engine_surface_backend_availability());
}

extern "C" VROEngineStatusCode viro_engine_surface_desc_validate_selected(
    const VROEngineSurfaceDesc *desc) {
    const auto backend = viro_engine_surface_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_surface_desc_validate(desc);
    }
#if defined(VRO_ENGINE_ESKIU_SURFACE_AVAILABLE) && VRO_ENGINE_ESKIU_SURFACE_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_surface_desc_validate(
                const_cast<VROEngineSurfaceDesc *>(desc)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_surface_frame_validate_selected(
    const VROEngineSurfaceFrame *frame) {
    const auto backend = viro_engine_surface_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_surface_frame_validate(frame);
    }
#if defined(VRO_ENGINE_ESKIU_SURFACE_AVAILABLE) && VRO_ENGINE_ESKIU_SURFACE_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_surface_frame_validate(
                const_cast<VROEngineSurfaceFrame *>(frame)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" int32_t viro_engine_surface_frame_is_newer_selected(
    uint64_t consumed_frame_id,
    uint64_t candidate_frame_id) {
    const auto backend = viro_engine_surface_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_surface_frame_is_newer(
            consumed_frame_id, candidate_frame_id);
    }
#if defined(VRO_ENGINE_ESKIU_SURFACE_AVAILABLE) && VRO_ENGINE_ESKIU_SURFACE_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return viro_eskiu_surface_frame_is_newer(
            consumed_frame_id, candidate_frame_id);
    }
#endif
    return 0;
}
