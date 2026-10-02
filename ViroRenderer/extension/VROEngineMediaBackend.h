#ifndef VRO_ENGINE_MEDIA_BACKEND_H
#define VRO_ENGINE_MEDIA_BACKEND_H

#include <stdint.h>

#include "VROEngineBackendSelector.h"
#include "VROEngineMediaABI.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t viro_engine_media_backend_availability(void);
VROEngineBackendKind viro_engine_media_backend_resolve(void);

VROEngineStatusCode viro_engine_media_plane_validate_selected(
    const VROEngineMediaPlane *plane);
VROEngineStatusCode viro_engine_media_frame_validate_selected(
    const VROEngineMediaFrame *frame);
VROEngineStatusCode viro_engine_media_frame_payload_bytes_selected(
    const VROEngineMediaFrame *frame,
    uint64_t *out_bytes);

#ifdef __cplusplus
}
#endif

#endif
