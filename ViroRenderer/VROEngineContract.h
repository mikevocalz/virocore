#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_CONTRACT_MAJOR 1u
#define VRO_ENGINE_CONTRACT_MINOR 0u

typedef uint64_t VROEngineCapabilityBits;
typedef uint32_t VROEngineBackend;
typedef int32_t VROEngineStatus;

#define VRO_ENGINE_STATUS_OK 0
#define VRO_ENGINE_STATUS_INVALID_ARGUMENT (-1)
#define VRO_ENGINE_STATUS_VERSION_MISMATCH (-2)

#define VRO_ENGINE_BACKEND_UNKNOWN 0u
#define VRO_ENGINE_BACKEND_CPP 1u
#define VRO_ENGINE_BACKEND_ESKIU 2u
#define VRO_ENGINE_BACKEND_WASM 3u

#define VRO_ENGINE_CAP_OPENXR               (1ull << 0)
#define VRO_ENGINE_CAP_AR_TRACKING          (1ull << 1)
#define VRO_ENGINE_CAP_SPATIAL_ANCHORS      (1ull << 2)
#define VRO_ENGINE_CAP_SHARED_FRAMES        (1ull << 3)
#define VRO_ENGINE_CAP_WORLD_MESH           (1ull << 4)
#define VRO_ENGINE_CAP_CONTROLLER_INPUT     (1ull << 5)
#define VRO_ENGINE_CAP_HAND_TRACKING        (1ull << 6)
#define VRO_ENGINE_CAP_GAZE_INPUT           (1ull << 7)
#define VRO_ENGINE_CAP_STYLUS_INPUT         (1ull << 8)
#define VRO_ENGINE_CAP_CAMERA_TEXTURE       (1ull << 9)
#define VRO_ENGINE_CAP_RECORDING            (1ull << 10)
#define VRO_ENGINE_CAP_EXTERNAL_SURFACE     (1ull << 11)
#define VRO_ENGINE_CAP_DYNAMIC_GEOMETRY     (1ull << 12)
#define VRO_ENGINE_CAP_WEB_WASM             (1ull << 13)

typedef struct VROEngineContractInfo {
    uint32_t struct_size;
    uint16_t major;
    uint16_t minor;
    VROEngineBackend backend;
    uint32_t reserved0;
    VROEngineCapabilityBits capabilities;
    const char *backend_name;
    const char *build_id;
} VROEngineContractInfo;

/**
 * Returns (major << 16) | minor.
 */
uint32_t vro_engine_contract_version(void);

/**
 * Fills an ABI-stable snapshot for the current renderer backend.
 *
 * Callers must set out_info->struct_size to sizeof(VROEngineContractInfo).
 * backend_name/build_id are borrowed process-lifetime strings and must not be freed.
 */
VROEngineStatus vro_engine_contract_get_info(VROEngineContractInfo *out_info);

/**
 * Returns 1 when every bit in required is supported, else 0.
 */
int32_t vro_engine_contract_has_capabilities(VROEngineCapabilityBits required);

#ifdef __cplusplus
}
#endif
