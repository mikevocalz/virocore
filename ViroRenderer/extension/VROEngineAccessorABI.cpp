#include "VROEngineAccessorABI.h"

#include <cstring>
#include <limits>

namespace {

uint64_t sparseIndexAt(
    const VROEngineAccessorMaterializeDesc *desc,
    uint64_t i,
    bool *ok) {
    *ok = true;
    switch (desc->sparse_index_type) {
        case VRO_ENGINE_SPARSE_INDEX_UINT8:
            return desc->sparse_indices[i];
        case VRO_ENGINE_SPARSE_INDEX_UINT16:
            return reinterpret_cast<const uint16_t *>(desc->sparse_indices)[i];
        case VRO_ENGINE_SPARSE_INDEX_UINT32:
            return reinterpret_cast<const uint32_t *>(desc->sparse_indices)[i];
        default:
            *ok = false;
            return 0;
    }
}

}

extern "C" VROEngineStatusCode viro_engine_accessor_materialize_validate(
    const VROEngineAccessorMaterializeDesc *desc) {
    if (desc == nullptr ||
        desc->struct_size < VRO_ENGINE_ACCESSOR_MATERIALIZE_DESC_V0_1_SIZE ||
        desc->element_size == 0 ||
        desc->element_count == 0 ||
        desc->output.data == nullptr) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (desc->element_count >
        std::numeric_limits<uint64_t>::max() / desc->element_size) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t required =
        desc->element_count * static_cast<uint64_t>(desc->element_size);
    if (desc->output.length < required) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (desc->base_data != nullptr && desc->base_stride < desc->element_size) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (desc->sparse_count == 0) {
        return desc->sparse_index_type == VRO_ENGINE_SPARSE_INDEX_NONE
            ? VRO_ENGINE_STATUS_OK
            : VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (desc->sparse_count > desc->element_count ||
        desc->sparse_indices == nullptr ||
        desc->sparse_values == nullptr ||
        desc->sparse_index_type < VRO_ENGINE_SPARSE_INDEX_UINT8 ||
        desc->sparse_index_type > VRO_ENGINE_SPARSE_INDEX_UINT32) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineStatusCode viro_engine_accessor_materialize(
    const VROEngineAccessorMaterializeDesc *desc) {
    const auto status = viro_engine_accessor_materialize_validate(desc);
    if (status != VRO_ENGINE_STATUS_OK) {
        return status;
    }

    const uint64_t total =
        desc->element_count * static_cast<uint64_t>(desc->element_size);
    std::memset(desc->output.data, 0, static_cast<size_t>(total));

    if (desc->base_data != nullptr) {
        for (uint64_t i = 0; i < desc->element_count; ++i) {
            std::memcpy(
                desc->output.data + i * desc->element_size,
                desc->base_data + i * desc->base_stride,
                desc->element_size);
        }
    }

    for (uint64_t i = 0; i < desc->sparse_count; ++i) {
        bool ok = false;
        const uint64_t index = sparseIndexAt(desc, i, &ok);
        if (!ok || index >= desc->element_count) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        std::memcpy(
            desc->output.data + index * desc->element_size,
            desc->sparse_values + i * desc->element_size,
            desc->element_size);
    }
    return VRO_ENGINE_STATUS_OK;
}
