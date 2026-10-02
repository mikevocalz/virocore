#include "VROEngineAccessorABI.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

int main() {
    uint8_t base[] = {
        1,2,3,4, 99,99,99,99,
        5,6,7,8, 99,99,99,99,
        9,10,11,12, 99,99,99,99
    };
    uint16_t sparseIndex[] = {1};
    uint8_t sparseValue[] = {40,41,42,43};
    uint8_t output[12]{};

    VROEngineAccessorMaterializeDesc d{};
    d.struct_size = sizeof(d);
    d.element_size = 4;
    d.element_count = 3;
    d.base_data = base;
    d.base_length = sizeof(base);
    d.base_stride = 8;
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_UINT16;
    d.sparse_count = 1;
    d.sparse_indices = reinterpret_cast<uint8_t *>(sparseIndex);
    d.sparse_indices_length = sizeof(sparseIndex);
    d.sparse_values = sparseValue;
    d.sparse_values_length = sizeof(sparseValue);
    d.output = {output, sizeof(output)};

    assert(viro_engine_accessor_materialize(&d) == VRO_ENGINE_STATUS_OK);
    const uint8_t expected[] = {1,2,3,4,40,41,42,43,9,10,11,12};
    assert(std::memcmp(output, expected, sizeof(expected)) == 0);

    d.base_data = nullptr;
    d.base_length = 0;
    d.base_stride = 0;
    d.sparse_count = 0;
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_NONE;
    d.sparse_indices = nullptr;
    d.sparse_indices_length = 0;
    d.sparse_values = nullptr;
    d.sparse_values_length = 0;
    std::memset(output, 0xff, sizeof(output));
    assert(viro_engine_accessor_materialize(&d) == VRO_ENGINE_STATUS_OK);
    for (uint8_t b : output) assert(b == 0);

    d.output.length = 2;
    assert(viro_engine_accessor_materialize(&d) ==
           VRO_ENGINE_STATUS_INVALID_ARGUMENT);

    d.output.length = sizeof(output);
    d.base_data = base;
    d.base_length = 4;
    d.base_stride = 8;
    assert(viro_engine_accessor_materialize(&d) ==
           VRO_ENGINE_STATUS_INVALID_ARGUMENT);

    d.base_data = nullptr;
    d.base_length = 0;
    d.base_stride = 0;
    d.sparse_count = 1;
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_UINT16;
    d.sparse_indices = reinterpret_cast<uint8_t *>(sparseIndex);
    d.sparse_indices_length = 1;
    d.sparse_values = sparseValue;
    d.sparse_values_length = sizeof(sparseValue);
    assert(viro_engine_accessor_materialize(&d) ==
           VRO_ENGINE_STATUS_INVALID_ARGUMENT);

    std::cout << "Engine accessor materialize ABI: PASS\n";
}
