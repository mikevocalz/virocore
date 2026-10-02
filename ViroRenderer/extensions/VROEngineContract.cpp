#include "VROEngineContract.h"

namespace {
constexpr const char *kBuildId = "virocore-cpp-reference";
}

extern "C" VROEngineContractInfo vro_engine_contract_info(void) {
    VROEngineContractInfo info{};
    info.struct_size = sizeof(VROEngineContractInfo);
    info.version_major = VIRO_ENGINE_CONTRACT_VERSION_MAJOR;
    info.version_minor = VIRO_ENGINE_CONTRACT_VERSION_MINOR;
    info.backend = VRO_ENGINE_BACKEND_CPP;
    info.capabilities = VRO_ENGINE_CAP_NONE;
    info.build_id = kBuildId;
    return info;
}

extern "C" VROEngineStatus vro_engine_contract_validate(uint32_t expected_major) {
    VROEngineStatus status{};
    status.struct_size = sizeof(VROEngineStatus);
    status.code = expected_major == VIRO_ENGINE_CONTRACT_VERSION_MAJOR
        ? VRO_ENGINE_STATUS_OK
        : VRO_ENGINE_STATUS_UNSUPPORTED;
    status.recoverable = expected_major == VIRO_ENGINE_CONTRACT_VERSION_MAJOR ? 0u : 1u;
    return status;
}

extern "C" const char *vro_engine_backend_name(uint32_t backend) {
    switch (backend) {
        case VRO_ENGINE_BACKEND_CPP:
            return "cpp";
        case VRO_ENGINE_BACKEND_ESKIU:
            return "eskiu";
        case VRO_ENGINE_BACKEND_WASM:
            return "wasm";
        default:
            return "unknown";
    }
}
