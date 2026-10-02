#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ABI 0.1 is already consumed by the React/TypeScript facade in
 * components/Engine/ViroEngineContract.ts.
 */
#define VRO_ENGINE_ABI_MAJOR 0u
#define VRO_ENGINE_ABI_MINOR 1u
#define VRO_ENGINE_ABI_INFO_SIZE 24u

typedef uint32_t VROEngineBackendKind;
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

/*
 * Keep this prefix ABI-stable. JS transports the 64-bit capability mask as two
 * uint32 words because JavaScript numbers cannot represent arbitrary uint64.
 *
 * Layout:
 *  0  uint32 struct_size
 *  4  uint16 abi_major
 *  6  uint16 abi_minor
 *  8  uint32 backend_kind
 * 12  uint32 reserved0
 * 16  uint32 capabilities_low
 * 20  uint32 capabilities_high
 */
typedef struct VROEngineAbiInfo {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    VROEngineBackendKind backend_kind;
    uint32_t reserved0;
    uint32_t capabilities_low;
    uint32_t capabilities_high;
} VROEngineAbiInfo;

uint32_t vro_engine_contract_version(void);

VROEngineStatus vro_engine_contract_query(VROEngineAbiInfo *out_info);

int32_t vro_engine_contract_has_capabilities(
    uint32_t required_low,
    uint32_t required_high);

const char *vro_engine_contract_backend_name(void);
const char *vro_engine_contract_build_id(void);

#ifdef __cplusplus
}
#endif
