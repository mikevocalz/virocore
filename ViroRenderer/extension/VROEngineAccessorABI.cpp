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
        case VRO_ENGINE_SPARSE_INDEX_UINT16: {
            uint16_t value = 0;
            std::memcpy(&value, desc->sparse_indices + i * sizeof(value), sizeof(value));
            return value;
        }
        case VRO_ENGINE_SPARSE_INDEX_UINT32: {
            uint32_t value = 0;
            std::memcpy(&value, desc->sparse_indices + i * sizeof(value), sizeof(value));
            return value;
        }
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

    const uint64_t maxSize = static_cast<uint64_t>(
        std::numeric_limits<size_t>::max());

    if (desc->element_count >
        std::numeric_limits<uint64_t>::max() / desc->element_size) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t required =
        desc->element_count * static_cast<uint64_t>(desc->element_size);
    if (required > maxSize || desc->output.length < required) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    if (desc->base_data == nullptr) {
        if (desc->base_length != 0 || desc->base_stride != 0) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
    } else {
        if (desc->base_stride < desc->element_size) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        uint64_t baseRequired = desc->element_size;
        if (desc->element_count > 1) {
            const uint64_t steps = desc->element_count - 1;
            if (steps >
                (std::numeric_limits<uint64_t>::max() - desc->element_size) /
                    desc->base_stride) {
                return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
            }
            baseRequired = steps * desc->base_stride + desc->element_size;
        }
        if (baseRequired > maxSize || desc->base_length < baseRequired) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
    }

    if (desc->sparse_count == 0) {
        if (desc->sparse_index_type != VRO_ENGINE_SPARSE_INDEX_NONE ||
            desc->sparse_indices != nullptr ||
            desc->sparse_indices_length != 0 ||
            desc->sparse_values != nullptr ||
            desc->sparse_values_length != 0) {
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        return VRO_ENGINE_STATUS_OK;
    }

    if (desc->sparse_count > desc->element_count ||
        desc->sparse_indices == nullptr ||
        desc->sparse_values == nullptr ||
        desc->sparse_index_type < VRO_ENGINE_SPARSE_INDEX_UINT8 ||
        desc->sparse_index_type > VRO_ENGINE_SPARSE_INDEX_UINT32) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    uint64_t indexSize = 0;
    switch (desc->sparse_index_type) {
        case VRO_ENGINE_SPARSE_INDEX_UINT8: indexSize = 1; break;
        case VRO_ENGINE_SPARSE_INDEX_UINT16: indexSize = 2; break;
        case VRO_ENGINE_SPARSE_INDEX_UINT32: indexSize = 4; break;
        default: return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    if (desc->sparse_count > std::numeric_limits<uint64_t>::max() / indexSize ||
        desc->sparse_count >
            std::numeric_limits<uint64_t>::max() / desc->element_size) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t sparseIndexBytes = desc->sparse_count * indexSize;
    const uint64_t sparseValueBytes =
        desc->sparse_count * static_cast<uint64_t>(desc->element_size);
    if (sparseIndexBytes > maxSize || sparseValueBytes > maxSize ||
        desc->sparse_indices_length < sparseIndexBytes ||
        desc->sparse_values_length < sparseValueBytes) {
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
