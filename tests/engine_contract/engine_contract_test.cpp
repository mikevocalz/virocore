#include "../../ViroRenderer/VROEngineContract.h"

#include <cassert>
#include <cstring>

int main() {
    static_assert(sizeof(VROEngineAbiInfo) == 24, "ABI prefix must stay 24 bytes");

    assert(vro_engine_contract_version() ==
           ((VRO_ENGINE_ABI_MAJOR << 16) | VRO_ENGINE_ABI_MINOR));

    assert(vro_engine_contract_query(nullptr) ==
           VRO_ENGINE_STATUS_INVALID_ARGUMENT);

    VROEngineAbiInfo too_small{};
    too_small.struct_size = sizeof(uint32_t);
    assert(vro_engine_contract_query(&too_small) ==
           VRO_ENGINE_STATUS_VERSION_MISMATCH);

    VROEngineAbiInfo info{};
    info.struct_size = sizeof(info);
    assert(vro_engine_contract_query(&info) == VRO_ENGINE_STATUS_OK);
    assert(info.struct_size == 24);
    assert(info.abi_major == 0);
    assert(info.abi_minor == 1);
    assert(info.backend_kind == VRO_ENGINE_BACKEND_CPP);
    assert((info.capabilities_low & (1u << 12)) != 0u);
    assert(info.capabilities_high == 0u);

    assert(vro_engine_contract_has_capabilities(1u << 12, 0u) == 1);
    assert(vro_engine_contract_has_capabilities(1u << 8, 0u) == 0);

    assert(std::strcmp(vro_engine_contract_backend_name(), "cpp") == 0);
    assert(vro_engine_contract_build_id() != nullptr);

    return 0;
}
