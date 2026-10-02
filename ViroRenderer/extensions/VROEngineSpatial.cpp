#include "VROEngineSpatial.h"

extern "C" VROEngineSpatialTransform vro_engine_identity_transform(uint32_t frame_kind,
                                                                   uint64_t frame_id) {
    VROEngineSpatialTransform value{};
    value.struct_size = sizeof(VROEngineSpatialTransform);
    value.frame_kind = frame_kind;
    value.frame_id = frame_id;
    value.pose.orientation[3] = 1.0f;
    value.scale[0] = 1.0f;
    value.scale[1] = 1.0f;
    value.scale[2] = 1.0f;
    return value;
}
