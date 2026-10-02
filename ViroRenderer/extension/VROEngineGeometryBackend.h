#ifndef VRO_ENGINE_GEOMETRY_BACKEND_H
#define VRO_ENGINE_GEOMETRY_BACKEND_H

#include <stdint.h>

#include "VROEngineBackendSelector.h"
#include "VROEngineGeometryABI.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t viro_engine_geometry_backend_availability(void);

VROEngineBackendKind viro_engine_geometry_backend_resolve(void);

VROEngineStatusCode viro_engine_geometry_validate_selected(
    const VROEngineGeometryDesc *desc);

VROEngineStatusCode viro_engine_geometry_range_validate_selected(
    const VROEngineGeometryDesc *desc,
    const VROEngineGeometryRangeUpdate *update);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VRO_ENGINE_GEOMETRY_BACKEND_H
