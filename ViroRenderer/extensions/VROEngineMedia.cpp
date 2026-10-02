#include "VROEngineMedia.h"

extern "C" VROEngineStatus vro_engine_validate_media_frame(const VROEngineMediaFrame *frame) {
    if (frame == nullptr || frame->struct_size != sizeof(VROEngineMediaFrame)) {
        VROEngineStatus status{};
        status.struct_size = sizeof(VROEngineStatus);
        status.code = VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        status.recoverable = 1u;
        return status;
    }
    return vro_engine_validate_surface(&frame->surface);
}
