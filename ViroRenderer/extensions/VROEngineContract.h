#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define VIRO_ENGINE_CONTRACT_VERSION_MAJOR 1u
#define VIRO_ENGINE_CONTRACT_VERSION_MINOR 0u

typedef uint64_t VROEngineHandle;

typedef enum VROEngineBackend {
    VRO_ENGINE_BACKEND_CPP = 0,
    VRO_ENGINE_BACKEND_ESKIU = 1,
    VRO_ENGINE_BACKEND_WASM = 2
} VROEngineBackend;

typedef enum VROEngineCapability : uint64_t {
    VRO_ENGINE_CAP_NONE = 0,
    VRO_ENGINE_CAP_OPENXR = 1ull << 0,
    VRO_ENGINE_CAP_AR_TRACKING = 1ull << 1,
    VRO_ENGINE_CAP_SPATIAL_ANCHORS = 1ull << 2,
    VRO_ENGINE_CAP_SHARED_FRAMES = 1ull << 3,
    VRO_ENGINE_CAP_WORLD_MESH = 1ull << 4,
    VRO_ENGINE_CAP_CONTROLLER = 1ull << 5,
    VRO_ENGINE_CAP_HAND_TRACKING = 1ull << 6,
    VRO_ENGINE_CAP_GAZE = 1ull << 7,
    VRO_ENGINE_CAP_STYLUS = 1ull << 8,
    VRO_ENGINE_CAP_CAMERA_TEXTURE = 1ull << 9,
    VRO_ENGINE_CAP_RECORDING = 1ull << 10,
    VRO_ENGINE_CAP_EXTERNAL_SURFACES = 1ull << 11,
    VRO_ENGINE_CAP_DYNAMIC_GEOMETRY = 1ull << 12
} VROEngineCapability;

typedef struct VROEngineContractInfo {
    uint32_t struct_size;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t backend;
    uint64_t capabilities;
    const char *build_id;
} VROEngineContractInfo;

typedef enum VROEngineStatusCode {
    VRO_ENGINE_STATUS_OK = 0,
    VRO_ENGINE_STATUS_UNSUPPORTED = 1,
    VRO_ENGINE_STATUS_INVALID_ARGUMENT = 2,
    VRO_ENGINE_STATUS_STALE_HANDLE = 3,
    VRO_ENGINE_STATUS_BACKEND_ERROR = 4
} VROEngineStatusCode;

typedef struct VROEngineStatus {
    uint32_t struct_size;
    uint32_t code;
    uint32_t recoverable;
    uint32_t reserved;
} VROEngineStatus;

VROEngineContractInfo vro_engine_contract_info(void);
VROEngineStatus vro_engine_contract_validate(uint32_t expected_major);
const char *vro_engine_backend_name(uint32_t backend);

#ifdef __cplusplus
}
#endif
