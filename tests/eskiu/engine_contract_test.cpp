#include "../../ViroRenderer/extensions/VROEngineContract.h"

#include <cassert>
#include <cstring>

int main() {
    const auto info = vro_engine_contract_info();
    assert(info.struct_size == sizeof(VROEngineContractInfo));
    assert(info.version_major == 1u);
    assert(info.backend == VRO_ENGINE_BACKEND_CPP);
    assert(std::strcmp(vro_engine_backend_name(info.backend), "cpp") == 0);

    const auto ok = vro_engine_contract_validate(1u);
    assert(ok.code == VRO_ENGINE_STATUS_OK);

    const auto mismatch = vro_engine_contract_validate(2u);
    assert(mismatch.code == VRO_ENGINE_STATUS_UNSUPPORTED);
    assert(mismatch.recoverable == 1u);
    return 0;
}
