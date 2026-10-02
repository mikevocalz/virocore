#ifndef VRO_ENGINE_BACKEND_SELECTOR_H
#define VRO_ENGINE_BACKEND_SELECTOR_H

#include <stdint.h>
#include "VROEngineABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_BACKEND_SELECTOR_ABI_MAJOR 0u
#define VRO_ENGINE_BACKEND_SELECTOR_ABI_MINOR 1u

typedef enum VROEngineDomain {
    VRO_ENGINE_DOMAIN_INPUT = 0,
    VRO_ENGINE_DOMAIN_SPATIAL = 1,
    VRO_ENGINE_DOMAIN_SURFACE = 2,
    VRO_ENGINE_DOMAIN_MEDIA = 3,
    VRO_ENGINE_DOMAIN_GEOMETRY = 4,
    VRO_ENGINE_DOMAIN_WORLD_MESH = 5,
    VRO_ENGINE_DOMAIN_ASSET_LOADING = 6,
    VRO_ENGINE_DOMAIN_COUNT = 7
} VROEngineDomain;

typedef enum VROEngineBackendPreference {
    VRO_ENGINE_BACKEND_PREFERENCE_AUTO = 0,
    VRO_ENGINE_BACKEND_PREFERENCE_CPP = 1,
    VRO_ENGINE_BACKEND_PREFERENCE_ESKIU = 2
} VROEngineBackendPreference;

typedef enum VROEngineBackendAvailabilityBit {
    VRO_ENGINE_BACKEND_AVAILABLE_CPP = 1u << 0,
    VRO_ENGINE_BACKEND_AVAILABLE_ESKIU = 1u << 1
} VROEngineBackendAvailabilityBit;

VROEngineStatusCode viro_engine_backend_preference_set(
    VROEngineDomain domain,
    VROEngineBackendPreference preference);

VROEngineBackendPreference viro_engine_backend_preference_get(
    VROEngineDomain domain);

VROEngineBackendKind viro_engine_backend_resolve(
    VROEngineDomain domain,
    uint32_t available_backends);

void viro_engine_backend_preferences_reset(void);

#ifdef __cplusplus
}
#endif

#endif
