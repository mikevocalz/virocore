#ifndef VRO_ENGINE_ACCESSOR_BACKEND_H
#define VRO_ENGINE_ACCESSOR_BACKEND_H

#include <stdint.h>

#include "VROEngineAccessorABI.h"
#include "VROEngineBackendSelector.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t viro_engine_accessor_backend_availability(void);
VROEngineBackendKind viro_engine_accessor_backend_resolve(void);

VROEngineStatusCode viro_engine_accessor_materialize_validate_selected(
    const VROEngineAccessorMaterializeDesc *desc);

VROEngineStatusCode viro_engine_accessor_materialize_selected(
    const VROEngineAccessorMaterializeDesc *desc);

#ifdef __cplusplus
}
#endif

#endif
