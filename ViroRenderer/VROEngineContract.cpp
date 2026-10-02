#include "VROEngineContract.h"

#include <stddef.h>

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
constexpr uint64_t kCapabilities =
    static_cast<uint64_t>(VRO_ENGINE_CAPABILITIES);

constexpr uint32_t packVersion() {
    return (VRO_ENGINE_ABI_MAJOR << 16) | VRO_ENGINE_ABI_MINOR;
}

constexpr uint32_t lowWord(uint64_t value) {
    return static_cast<uint32_t>(value & 0xffffffffull);
}

constexpr uint32_t highWord(uint64_t value) {
    return static_cast<uint32_t>((value >> 32) & 0xffffffffull);
}

static_assert(sizeof(VROEngineAbiInfo) == VRO_ENGINE_ABI_INFO_SIZE,
              "VROEngineAbiInfo must remain a 24-byte ABI prefix");
static_assert(offsetof(VROEngineAbiInfo, capabilities_low) == 16,
              "capabilities_low offset is part of the ABI");
static_assert(offsetof(VROEngineAbiInfo, capabilities_high) == 20,
              "capabilities_high offset is part of the ABI");
}

extern "C" uint32_t vro_engine_contract_version(void) {
    return packVersion();
}

extern "C" VROEngineStatus
vro_engine_contract_query(VROEngineAbiInfo *out_info) {
    if (out_info == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    if (out_info->struct_size < VRO_ENGINE_ABI_INFO_SIZE) {
        return VRO_ENGINE_STATUS_VERSION_MISMATCH;
    }

    out_info->abi_major = static_cast<uint16_t>(VRO_ENGINE_ABI_MAJOR);
    out_info->abi_minor = static_cast<uint16_t>(VRO_ENGINE_ABI_MINOR);
    out_info->backend_kind = static_cast<VROEngineBackendKind>(VRO_ENGINE_BACKEND_KIND);
    out_info->reserved0 = 0;
    out_info->capabilities_low = lowWord(kCapabilities);
    out_info->capabilities_high = highWord(kCapabilities);
    return VRO_ENGINE_STATUS_OK;
}

extern "C" int32_t vro_engine_contract_has_capabilities(
    uint32_t required_low,
    uint32_t required_high) {
    const uint64_t required =
        static_cast<uint64_t>(required_low) |
        (static_cast<uint64_t>(required_high) << 32);
    return (kCapabilities & required) == required ? 1 : 0;
}

extern "C" const char *vro_engine_contract_backend_name(void) {
    return VRO_ENGINE_BACKEND_NAME;
}

extern "C" const char *vro_engine_contract_build_id(void) {
    return VRO_ENGINE_BUILD_ID;
}
