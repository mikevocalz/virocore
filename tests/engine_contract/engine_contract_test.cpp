#include "../../ViroRenderer/VROEngineContract.h"

#include <cassert>
#include <cstring>

int main() {
    assert(vro_engine_contract_version() ==
           ((VRO_ENGINE_CONTRACT_MAJOR << 16) | VRO_ENGINE_CONTRACT_MINOR));

    assert(vro_engine_contract_get_info(nullptr) ==
           VRO_ENGINE_STATUS_INVALID_ARGUMENT);

    VROEngineContractInfo too_small{};
    too_small.struct_size = sizeof(uint32_t);
    assert(vro_engine_contract_get_info(&too_small) ==
           VRO_ENGINE_STATUS_VERSION_MISMATCH);

    VROEngineContractInfo info{};
    info.struct_size = sizeof(info);
    assert(vro_engine_contract_get_info(&info) == VRO_ENGINE_STATUS_OK);
    assert(info.major == VRO_ENGINE_CONTRACT_MAJOR);
    assert(info.minor == VRO_ENGINE_CONTRACT_MINOR);
    assert(info.backend == VRO_ENGINE_BACKEND_CPP);
    assert(info.backend_name != nullptr);
    assert(std::strcmp(info.backend_name, "cpp") == 0);
    assert(info.build_id != nullptr);

    assert(vro_engine_contract_has_capabilities(
               VRO_ENGINE_CAP_DYNAMIC_GEOMETRY) == 1);
    assert(vro_engine_contract_has_capabilities(
               VRO_ENGINE_CAP_STYLUS_INPUT) == 0);

    return 0;
}
