#include "VROEngineSurfaceABI.h"

int viro_surface_c_header_probe(void) {
    return sizeof(VROEngineSurfaceDesc) == VRO_ENGINE_SURFACE_DESC_V0_1_SIZE &&
           sizeof(VROEngineSurfaceFrame) == VRO_ENGINE_SURFACE_FRAME_V0_1_SIZE;
}
