#include "VROEngineAccessorBackend.h"

#if defined(VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE) && VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE
extern "C" {
int viro_eskiu_accessor_materialize_validate(
    VROEngineAccessorMaterializeDesc *desc);
int viro_eskiu_accessor_materialize(
    VROEngineAccessorMaterializeDesc *desc);
}
#endif

extern "C" uint32_t viro_engine_accessor_backend_availability(void) {
    uint32_t available = VRO_ENGINE_BACKEND_AVAILABLE_CPP;
#if defined(VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE) && VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE
    available |= VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;
#endif
    return available;
}

extern "C" VROEngineBackendKind viro_engine_accessor_backend_resolve(void) {
    return viro_engine_backend_resolve(
        VRO_ENGINE_DOMAIN_ASSET_LOADING,
        viro_engine_accessor_backend_availability());
}

extern "C" VROEngineStatusCode
viro_engine_accessor_materialize_validate_selected(
    const VROEngineAccessorMaterializeDesc *desc) {
    const auto backend = viro_engine_accessor_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_accessor_materialize_validate(desc);
    }
#if defined(VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE) && VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_accessor_materialize_validate(
                const_cast<VROEngineAccessorMaterializeDesc *>(desc)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_accessor_materialize_selected(
    const VROEngineAccessorMaterializeDesc *desc) {
    const auto backend = viro_engine_accessor_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_accessor_materialize(desc);
    }
#if defined(VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE) && VRO_ENGINE_ESKIU_ACCESSOR_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_accessor_materialize(
                const_cast<VROEngineAccessorMaterializeDesc *>(desc)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}
