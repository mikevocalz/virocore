#include "VROEngineGeometryBackend.h"

#if defined(VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE) && VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE
extern "C" {
int viro_eskiu_geometry_validate(VROEngineGeometryDesc *desc);
int viro_eskiu_geometry_range_validate(
    VROEngineGeometryDesc *desc,
    VROEngineGeometryRangeUpdate *update);
int viro_eskiu_geometry_pad_copy(
    uint8_t *active_data,
    uint64_t active_bytes,
    uint8_t *output_data,
    uint64_t output_bytes);
}
#endif

extern "C" uint32_t viro_engine_geometry_backend_availability(void) {
    uint32_t available = VRO_ENGINE_BACKEND_AVAILABLE_CPP;
#if defined(VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE) && VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE
    available |= VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;
#endif
    return available;
}

extern "C" VROEngineBackendKind viro_engine_geometry_backend_resolve(void) {
    return viro_engine_backend_resolve(
        VRO_ENGINE_DOMAIN_GEOMETRY,
        viro_engine_geometry_backend_availability());
}

extern "C" VROEngineStatusCode viro_engine_geometry_validate_selected(
    const VROEngineGeometryDesc *desc) {
    const VROEngineBackendKind backend = viro_engine_geometry_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_geometry_validate(desc);
    }
#if defined(VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE) && VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_geometry_validate(
                const_cast<VROEngineGeometryDesc *>(desc)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_geometry_range_validate_selected(
    const VROEngineGeometryDesc *desc,
    const VROEngineGeometryRangeUpdate *update) {
    const VROEngineBackendKind backend = viro_engine_geometry_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_geometry_range_validate(desc, update);
    }
#if defined(VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE) && VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_geometry_range_validate(
                const_cast<VROEngineGeometryDesc *>(desc),
                const_cast<VROEngineGeometryRangeUpdate *>(update)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_geometry_pad_copy_selected(
    const uint8_t *active_data,
    uint64_t active_bytes,
    uint8_t *output_data,
    uint64_t output_bytes) {
    const VROEngineBackendKind backend = viro_engine_geometry_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_geometry_pad_copy(
            active_data, active_bytes, output_data, output_bytes);
    }
#if defined(VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE) && VRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_geometry_pad_copy(
                const_cast<uint8_t *>(active_data),
                active_bytes,
                output_data,
                output_bytes));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}
