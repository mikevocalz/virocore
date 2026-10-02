#ifndef VRO_ENGINE_ACCESSOR_ABI_H
#define VRO_ENGINE_ACCESSOR_ABI_H

#include <stdint.h>
#include "VROEngineABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_ACCESSOR_ABI_MAJOR 0u
#define VRO_ENGINE_ACCESSOR_ABI_MINOR 1u

typedef enum VROEngineSparseIndexType {
    VRO_ENGINE_SPARSE_INDEX_NONE = 0,
    VRO_ENGINE_SPARSE_INDEX_UINT8 = 1,
    VRO_ENGINE_SPARSE_INDEX_UINT16 = 2,
    VRO_ENGINE_SPARSE_INDEX_UINT32 = 3
} VROEngineSparseIndexType;

typedef struct VROEngineAccessorMaterializeDesc {
    uint32_t struct_size;
    uint32_t element_size;
    uint64_t element_count;

    const uint8_t *base_data;
    uint64_t base_stride;

    uint32_t sparse_index_type;
    uint32_t reserved0;
    uint64_t sparse_count;
    const uint8_t *sparse_indices;
    const uint8_t *sparse_values;

    VROEngineMutableByteView output;
} VROEngineAccessorMaterializeDesc;

/*
 * This ABI contains native pointers, so its frozen native layout differs by
 * pointer width. The field order/meaning is stable; size follows the target C ABI.
 */
#if UINTPTR_MAX == UINT64_MAX
#define VRO_ENGINE_ACCESSOR_MATERIALIZE_DESC_V0_1_SIZE 80u
#elif UINTPTR_MAX == UINT32_MAX
#define VRO_ENGINE_ACCESSOR_MATERIALIZE_DESC_V0_1_SIZE 72u
#else
#error "Unsupported pointer width for VROEngineAccessorMaterializeDesc"
#endif

VROEngineStatusCode viro_engine_accessor_materialize_validate(
    const VROEngineAccessorMaterializeDesc *desc);

VROEngineStatusCode viro_engine_accessor_materialize(
    const VROEngineAccessorMaterializeDesc *desc);

#ifdef __cplusplus
}
static_assert(sizeof(VROEngineAccessorMaterializeDesc) ==
              VRO_ENGINE_ACCESSOR_MATERIALIZE_DESC_V0_1_SIZE,
              "Accessor materialize ABI v0.1 must stay 80 bytes");
#endif

#endif
