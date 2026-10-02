//
// VROEngineSurfaceABI.cpp
//

#include "VROEngineSurfaceABI.h"

namespace {

bool validFormat(uint32_t format) {
    return format >= VRO_ENGINE_PIXEL_RGBA8_UNORM &&
           format <= VRO_ENGINE_PIXEL_DEPTH32_FLOAT;
}

bool validOrigin(uint32_t origin) {
    return origin <= VRO_ENGINE_SURFACE_ORIGIN_BOTTOM_LEFT;
}

} // namespace

extern "C" VROEngineStatusCode viro_engine_surface_desc_validate(
    const VROEngineSurfaceDesc *desc) {
    if (desc == nullptr ||
        desc->struct_size < VRO_ENGINE_SURFACE_DESC_V0_1_SIZE ||
        desc->width == 0 ||
        desc->height == 0 ||
        !validFormat(desc->pixel_format) ||
        !validOrigin(desc->origin) ||
        desc->plane_count == 0) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_surface_frame_validate(
    const VROEngineSurfaceFrame *frame) {
    if (frame == nullptr ||
        frame->struct_size < VRO_ENGINE_SURFACE_FRAME_V0_1_SIZE ||
        frame->surface == 0) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    return VRO_ENGINE_STATUS_OK;
}

extern "C" int32_t viro_engine_surface_frame_is_newer(
    uint64_t consumed_frame_id,
    uint64_t candidate_frame_id) {
    return candidate_frame_id > consumed_frame_id ? 1 : 0;
}
