#include "VROEngineInput.h"

extern "C" VROEngineStatus vro_engine_validate_input_batch(const VROEngineInputBatch *batch) {
    VROEngineStatus status{};
    status.struct_size = sizeof(VROEngineStatus);

    if (batch == nullptr || batch->struct_size != sizeof(VROEngineInputBatch)) {
        status.code = VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        status.recoverable = 1u;
        return status;
    }
    if (batch->count > 0 && batch->samples == nullptr) {
        status.code = VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        status.recoverable = 1u;
        return status;
    }

    status.code = VRO_ENGINE_STATUS_OK;
    return status;
}
