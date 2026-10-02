//
// VROEngineGeometryABI.cpp
//

#include "VROEngineGeometryABI.h"

#include <cstring>
#include <limits>

namespace {

bool validTopology(uint32_t value) {
    return value >= VRO_ENGINE_TOPOLOGY_POINTS &&
           value <= VRO_ENGINE_TOPOLOGY_TRIANGLE_STRIP;
}

uint32_t indexStride(uint32_t type) {
    switch (type) {
        case VRO_ENGINE_INDEX_NONE: return 0;
        case VRO_ENGINE_INDEX_UINT16: return 2;
        case VRO_ENGINE_INDEX_UINT32: return 4;
        default: return std::numeric_limits<uint32_t>::max();
    }
}

bool validView(VROEngineByteView view, uint64_t needed) {
    if (needed == 0) return view.length == 0 || view.data != nullptr;
    return view.data != nullptr && view.length >= needed;
}

} // namespace

extern "C" VROEngineStatusCode viro_engine_geometry_validate(
    const VROEngineGeometryDesc *desc) {
    if (desc == nullptr ||
        desc->struct_size < VRO_ENGINE_GEOMETRY_DESC_V0_1_SIZE ||
        !validTopology(desc->topology) ||
        desc->vertex_stride_bytes == 0 ||
        desc->vertex_count == 0 ||
        desc->attribute_count == 0 ||
        desc->attributes == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    const uint64_t vertexBytes =
        static_cast<uint64_t>(desc->vertex_stride_bytes) * desc->vertex_count;
    if (!validView(desc->vertices, vertexBytes)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < desc->attribute_count; ++i) {
        const auto &attribute = desc->attributes[i];
        if (attribute.component_count == 0 ||
            attribute.offset_bytes >= desc->vertex_stride_bytes) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
    }

    const uint32_t stride = indexStride(desc->index_type);
    if (stride == std::numeric_limits<uint32_t>::max()) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    if (desc->index_type == VRO_ENGINE_INDEX_NONE) {
        if (desc->index_count != 0) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
    } else {
        const uint64_t indexBytes =
            static_cast<uint64_t>(stride) * desc->index_count;
        if (!validView(desc->indices, indexBytes)) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
    }

    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_geometry_range_validate(
    const VROEngineGeometryDesc *desc,
    const VROEngineGeometryRangeUpdate *update) {
    if (viro_engine_geometry_validate(desc) != VRO_ENGINE_STATUS_OK ||
        update == nullptr ||
        update->struct_size < VRO_ENGINE_GEOMETRY_RANGE_UPDATE_V0_1_SIZE ||
        update->geometry_id != desc->geometry_id ||
        update->base_version != desc->version ||
        update->next_version <= update->base_version ||
        update->stream_kind > 1 ||
        update->bytes.data == nullptr ||
        update->bytes.length == 0) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    const uint64_t total =
        update->stream_kind == 0 ? desc->vertices.length : desc->indices.length;
    if (update->offset_bytes > total ||
        update->bytes.length > total - update->offset_bytes) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_geometry_pad_copy(
    const uint8_t *active_data,
    uint64_t active_bytes,
    uint8_t *output_data,
    uint64_t output_bytes) {
    if (output_data == nullptr ||
        output_bytes < active_bytes ||
        (active_bytes != 0 && active_data == nullptr)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (active_bytes != 0) {
        std::memcpy(output_data, active_data, static_cast<size_t>(active_bytes));
    }
    if (output_bytes > active_bytes) {
        std::memset(
            output_data + active_bytes,
            0,
            static_cast<size_t>(output_bytes - active_bytes));
    }
    return VRO_ENGINE_STATUS_OK;
}
