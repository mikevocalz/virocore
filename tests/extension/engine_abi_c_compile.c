#include "VROEngineABI.h"

#include <stdint.h>

_Static_assert(sizeof(VROEngineHandle) == 8, "engine handles must be 64-bit");
_Static_assert(sizeof(VROEngineVec3) == 12, "vec3 C layout changed");
_Static_assert(sizeof(VROEngineQuat) == 16, "quat C layout changed");
_Static_assert(sizeof(VROEngineAbiInfo) >= VRO_ENGINE_ABI_INFO_V0_1_SIZE,
               "ABI info lost its frozen v0.1 prefix");

static VROEngineStatusCode (*query_fn)(VROEngineAbiInfo *) = viro_engine_query_abi;
static uint32_t (*version_fn)(void) = viro_engine_abi_version;

int viro_engine_abi_c_header_probe(void) {
    VROEngineAbiInfo info = {0};
    info.struct_size = (uint32_t) sizeof(info);

    const uint32_t expected_version =
        ((uint32_t) VRO_ENGINE_ABI_VERSION_MAJOR << 16u) |
        (uint32_t) VRO_ENGINE_ABI_VERSION_MINOR;

    if (version_fn() != expected_version) {
        return 0;
    }
    if (query_fn(&info) != VRO_ENGINE_STATUS_OK) {
        return 0;
    }
    return info.abi_major == VRO_ENGINE_ABI_VERSION_MAJOR &&
           info.abi_minor == VRO_ENGINE_ABI_VERSION_MINOR &&
           info.backend_kind == VRO_ENGINE_BACKEND_CPP;
}
