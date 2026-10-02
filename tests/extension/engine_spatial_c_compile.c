#include "VROEngineSpatialABI.h"

int viro_spatial_c_header_probe(void) {
    return sizeof(VROEngineRigidTransform) == VRO_ENGINE_RIGID_TRANSFORM_V0_1_SIZE &&
           sizeof(VROEngineSharedFrame) == VRO_ENGINE_SHARED_FRAME_V0_1_SIZE;
}
