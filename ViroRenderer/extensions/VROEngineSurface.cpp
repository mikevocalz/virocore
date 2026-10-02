#include "VROEngineSurface.h"

extern "C" VROEngineStatus vro_engine_validate_surface(const VROEngineSurfaceDescriptor *surface) {
    VROEngineStatus status{};
    status.struct_size = sizeof(VROEngineStatus);

    if (surface == nullptr ||
        surface->struct_size != sizeof(VROEngineSurfaceDescriptor) ||
        surface->width == 0 ||
        surface->height == 0) {
        status.code = VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        status.recoverable = 1u;
        return status;
    }

    status.code = VRO_ENGINE_STATUS_OK;
    return status;
}
