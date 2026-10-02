//
// VROEngineABI.h
//
// Experimental language-neutral extension ABI for ViroCore.
//
// This header is intentionally C-compatible. It is the seam between the existing
// C++ renderer and extension backends that may be implemented in Eskiu or another
// systems language. Do not expose STL, C++ class layouts, Objective-C objects,
// Java objects, or backend-native GPU objects through this boundary.
//

#ifndef VRO_ENGINE_ABI_H
#define VRO_ENGINE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_ABI_VERSION_MAJOR 0u
#define VRO_ENGINE_ABI_VERSION_MINOR 1u

typedef uint64_t VROEngineHandle;
typedef uint64_t VROEngineCapabilities;

typedef enum VROEngineStatusCode {
    VRO_ENGINE_STATUS_OK = 0,
    VRO_ENGINE_STATUS_UNSUPPORTED = 1,
    VRO_ENGINE_STATUS_INVALID_ARGUMENT = 2,
    VRO_ENGINE_STATUS_VERSION_MISMATCH = 3,
    VRO_ENGINE_STATUS_WRONG_THREAD = 4,
    VRO_ENGINE_STATUS_STALE_HANDLE = 5,
    VRO_ENGINE_STATUS_OUT_OF_MEMORY = 6,
    VRO_ENGINE_STATUS_INTERNAL_ERROR = 7
} VROEngineStatusCode;

typedef enum VROEngineBackendKind {
    VRO_ENGINE_BACKEND_UNKNOWN = 0,
    VRO_ENGINE_BACKEND_CPP = 1,
    VRO_ENGINE_BACKEND_ESKIU = 2,
    VRO_ENGINE_BACKEND_WASM = 3
} VROEngineBackendKind;

typedef enum VROEngineCapabilityBit {
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
    VRO_ENGINE_CAP_EXTERNAL_SURFACE = 1ull << 11,
    VRO_ENGINE_CAP_DYNAMIC_GEOMETRY = 1ull << 12
} VROEngineCapabilityBit;

typedef struct VROEngineAbiInfo {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t backend_kind;
    uint32_t reserved0;
    uint64_t capabilities;
} VROEngineAbiInfo;

typedef struct VROEngineVec3 {
    float x;
    float y;
    float z;
} VROEngineVec3;

typedef struct VROEngineQuat {
    float x;
    float y;
    float z;
    float w;
} VROEngineQuat;

typedef struct VROEnginePose {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t timestamp_ns;
    VROEngineVec3 position;
    VROEngineQuat orientation;
} VROEnginePose;

typedef struct VROEngineByteView {
    const uint8_t *data;
    uint64_t length;
} VROEngineByteView;

typedef struct VROEngineMutableByteView {
    uint8_t *data;
    uint64_t length;
} VROEngineMutableByteView;

/*
 * The first production function table will be added only after the C++ reference
 * implementation and conformance tests exist. Keeping the initial header data-only
 * prevents an accidental ABI freeze while still establishing layout/ownership rules.
 */

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(VROEngineHandle) == 8, "VROEngineHandle must remain 64-bit");
static_assert(sizeof(VROEngineVec3) == sizeof(float) * 3, "Unexpected VROEngineVec3 layout");
static_assert(sizeof(VROEngineQuat) == sizeof(float) * 4, "Unexpected VROEngineQuat layout");
#endif

#endif // VRO_ENGINE_ABI_H
