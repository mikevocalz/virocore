#include "VROEngineContract.h"

#ifndef VRO_ENGINE_BUILD_ID
#define VRO_ENGINE_BUILD_ID "unknown"
#endif

#ifndef VRO_ENGINE_BACKEND_NAME
#define VRO_ENGINE_BACKEND_NAME "cpp"
#endif

#ifndef VRO_ENGINE_BACKEND_KIND
#define VRO_ENGINE_BACKEND_KIND VRO_ENGINE_BACKEND_CPP
#endif

#ifndef VRO_ENGINE_CAPABILITIES
#define VRO_ENGINE_CAPABILITIES (VRO_ENGINE_CAP_DYNAMIC_GEOMETRY)
#endif

namespace {
constexpr uint32_t packVersion() {
    return (VRO_ENGINE_CONTRACT_MAJOR << 16) | VRO_ENGINE_CONTRACT_MINOR;
}

constexpr VROEngineCapabilityBits kCapabilities =
    static_cast<VROEngineCapabilityBits>(VRO_ENGINE_CAPABILITIES);
}

extern "C" uint32_t vro_engine_contract_version(void) {
    return packVersion();
}

extern "C" VROEngineStatus
vro_engine_contract_get_info(VROEngineContractInfo *out_info) {
    if (out_info == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (out_info->struct_size < sizeof(VROEngineContractInfo)) {
        return VRO_ENGINE_STATUS_VERSION_MISMATCH;
    }

    out_info->major = static_cast<uint16_t>(VRO_ENGINE_CONTRACT_MAJOR);
    out_info->minor = static_cast<uint16_t>(VRO_ENGINE_CONTRACT_MINOR);
    out_info->backend = static_cast<VROEngineBackend>(VRO_ENGINE_BACKEND_KIND);
    out_info->reserved0 = 0;
    out_info->capabilities = kCapabilities;
    out_info->backend_name = VRO_ENGINE_BACKEND_NAME;
    out_info->build_id = VRO_ENGINE_BUILD_ID;
    return VRO_ENGINE_STATUS_OK;
}

extern "C" int32_t
vro_engine_contract_has_capabilities(VROEngineCapabilityBits required) {
    return (kCapabilities & required) == required ? 1 : 0;
}
