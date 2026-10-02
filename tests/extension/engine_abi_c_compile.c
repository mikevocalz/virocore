#include "VROEngineABI.h"

#include <stdint.h>

_Static_assert(sizeof(VROEngineHandle) == 8, "engine handles must be 64-bit");
_Static_assert(sizeof(VROEngineVec3) == 12, "vec3 C layout changed");
_Static_assert(sizeof(VROEngineQuat) == 16, "quat C layout changed");

static VROEngineStatusCode (*query_fn)(VROEngineAbiInfo *) = viro_engine_query_abi;
static uint32_t (*version_fn)(void) = viro_engine_abi_version;

int viro_engine_abi_c_header_probe(void) {
    return query_fn != 0 && version_fn != 0;
}
