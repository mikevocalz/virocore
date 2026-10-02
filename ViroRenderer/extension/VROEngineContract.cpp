//
// VROEngineContract.cpp
//
// C++ reference implementation of the language-neutral engine contract.
// This file intentionally depends only on the ABI header. Production renderer
// capability adapters are layered on later PRs.
//

#include "VROEngineABI.h"

#include <cstring>

extern "C" uint32_t viro_engine_abi_version(void) {
    return (static_cast<uint32_t>(VRO_ENGINE_ABI_VERSION_MAJOR) << 16u) |
           static_cast<uint32_t>(VRO_ENGINE_ABI_VERSION_MINOR);
}

extern "C" VROEngineStatusCode viro_engine_query_abi(VROEngineAbiInfo *out_info) {
    if (out_info == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    const uint32_t caller_size = out_info->struct_size;
    if (caller_size < VRO_ENGINE_ABI_INFO_V0_1_SIZE) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    VROEngineAbiInfo info{};
    info.struct_size = static_cast<uint32_t>(sizeof(VROEngineAbiInfo));
    info.abi_major = static_cast<uint16_t>(VRO_ENGINE_ABI_VERSION_MAJOR);
    info.abi_minor = static_cast<uint16_t>(VRO_ENGINE_ABI_VERSION_MINOR);
    info.backend_kind = static_cast<uint32_t>(VRO_ENGINE_BACKEND_KIND);
    info.reserved0 = 0;
    // Capability wiring is deliberately compile-time and conservative until
    // runtime adapters exist. A platform may identify its backend while still
    // advertising zero production feature capabilities.
    info.capabilities = static_cast<uint64_t>(VRO_ENGINE_CAPABILITIES);

    const size_t write_size =
        caller_size < sizeof(info) ? static_cast<size_t>(caller_size) : sizeof(info);
    std::memcpy(out_info, &info, write_size);
    return VRO_ENGINE_STATUS_OK;
}
