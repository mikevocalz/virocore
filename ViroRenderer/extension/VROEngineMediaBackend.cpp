#include "VROEngineMediaBackend.h"

#if defined(VRO_ENGINE_ESKIU_MEDIA_AVAILABLE) && VRO_ENGINE_ESKIU_MEDIA_AVAILABLE
extern "C" {
int viro_eskiu_media_plane_validate(VROEngineMediaPlane *plane);
int viro_eskiu_media_frame_validate(VROEngineMediaFrame *frame);
int viro_eskiu_media_frame_payload_bytes(
    VROEngineMediaFrame *frame,
    uint64_t *out_bytes);
}
#endif

extern "C" uint32_t viro_engine_media_backend_availability(void) {
    uint32_t available = VRO_ENGINE_BACKEND_AVAILABLE_CPP;
#if defined(VRO_ENGINE_ESKIU_MEDIA_AVAILABLE) && VRO_ENGINE_ESKIU_MEDIA_AVAILABLE
    available |= VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;
#endif
    return available;
}

extern "C" VROEngineBackendKind viro_engine_media_backend_resolve(void) {
    return viro_engine_backend_resolve(
        VRO_ENGINE_DOMAIN_MEDIA,
        viro_engine_media_backend_availability());
}

extern "C" VROEngineStatusCode viro_engine_media_plane_validate_selected(
    const VROEngineMediaPlane *plane) {
    const auto backend = viro_engine_media_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_media_plane_validate(plane);
    }
#if defined(VRO_ENGINE_ESKIU_MEDIA_AVAILABLE) && VRO_ENGINE_ESKIU_MEDIA_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_media_plane_validate(
                const_cast<VROEngineMediaPlane *>(plane)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_media_frame_validate_selected(
    const VROEngineMediaFrame *frame) {
    const auto backend = viro_engine_media_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_media_frame_validate(frame);
    }
#if defined(VRO_ENGINE_ESKIU_MEDIA_AVAILABLE) && VRO_ENGINE_ESKIU_MEDIA_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_media_frame_validate(
                const_cast<VROEngineMediaFrame *>(frame)));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" VROEngineStatusCode viro_engine_media_frame_payload_bytes_selected(
    const VROEngineMediaFrame *frame,
    uint64_t *out_bytes) {
    const auto backend = viro_engine_media_backend_resolve();
    if (backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_media_frame_payload_bytes(frame, out_bytes);
    }
#if defined(VRO_ENGINE_ESKIU_MEDIA_AVAILABLE) && VRO_ENGINE_ESKIU_MEDIA_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineStatusCode>(
            viro_eskiu_media_frame_payload_bytes(
                const_cast<VROEngineMediaFrame *>(frame),
                out_bytes));
    }
#endif
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}
