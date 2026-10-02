#ifndef VRO_ENGINE_SURFACE_BACKEND_H
#define VRO_ENGINE_SURFACE_BACKEND_H

#include <stdint.h>

#include "VROEngineBackendSelector.h"
#include "VROEngineSurfaceABI.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t viro_engine_surface_backend_availability(void);
VROEngineBackendKind viro_engine_surface_backend_resolve(void);

VROEngineStatusCode viro_engine_surface_desc_validate_selected(
    const VROEngineSurfaceDesc *desc);
VROEngineStatusCode viro_engine_surface_frame_validate_selected(
    const VROEngineSurfaceFrame *frame);
int32_t viro_engine_surface_frame_is_newer_selected(
    uint64_t consumed_frame_id,
    uint64_t candidate_frame_id);

#ifdef __cplusplus
}
#endif

#endif
