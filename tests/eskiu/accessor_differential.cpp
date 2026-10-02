#include "VROEngineAccessorABI.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

extern "C" {
int viro_eskiu_accessor_materialize_validate(VROEngineAccessorMaterializeDesc *);
int viro_eskiu_accessor_materialize(VROEngineAccessorMaterializeDesc *);
}

static void fail(const char *label) {
    std::cerr << "FAIL " << label << "\n";
    std::exit(EXIT_FAILURE);
}

static void expectValidationSame(VROEngineAccessorMaterializeDesc d, const char *label) {
    const int cpp = (int)viro_engine_accessor_materialize_validate(&d);
    const int esk = viro_eskiu_accessor_materialize_validate(&d);
    if (cpp != esk) fail(label);
}

static void expectMaterializeSame(
    VROEngineAccessorMaterializeDesc d,
    uint8_t *cppOut,
    uint8_t *eskOut,
    size_t n,
    const char *label) {
    auto cppDesc = d;
    auto eskDesc = d;
    cppDesc.output = {cppOut, n};
    eskDesc.output = {eskOut, n};
    const int cpp = (int)viro_engine_accessor_materialize(&cppDesc);
    const int esk = viro_eskiu_accessor_materialize(&eskDesc);
    if (cpp != esk || (cpp == 0 && std::memcmp(cppOut, eskOut, n) != 0)) {
        fail(label);
    }
}

int main() {
    uint8_t base[] = {
        1,2,3,4, 99,99,99,99,
        5,6,7,8, 99,99,99,99,
        9,10,11,12, 99,99,99,99
    };
    uint16_t sparse16[] = {1};
    uint8_t sparseValue[] = {40,41,42,43};
    std::array<uint8_t,12> cppOut{}, eskOut{};

    VROEngineAccessorMaterializeDesc d{};
    d.struct_size = sizeof(d);
    d.element_size = 4;
    d.element_count = 3;
    d.base_data = base;
    d.base_length = sizeof(base);
    d.base_stride = 8;
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_UINT16;
    d.sparse_count = 1;
    d.sparse_indices = reinterpret_cast<uint8_t *>(sparse16);
    d.sparse_indices_length = sizeof(sparse16);
    d.sparse_values = sparseValue;
    d.sparse_values_length = sizeof(sparseValue);
    d.output = {cppOut.data(), cppOut.size()};

    expectValidationSame(d, "valid");
    expectMaterializeSame(d, cppOut.data(), eskOut.data(), cppOut.size(), "uint16 sparse");

    uint8_t sparse8[] = {2};
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_UINT8;
    d.sparse_indices = sparse8;
    d.sparse_indices_length = sizeof(sparse8);
    cppOut.fill(0); eskOut.fill(0);
    expectMaterializeSame(d, cppOut.data(), eskOut.data(), cppOut.size(), "uint8 sparse");

    uint32_t sparse32[] = {0};
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_UINT32;
    d.sparse_indices = reinterpret_cast<uint8_t *>(sparse32);
    d.sparse_indices_length = sizeof(sparse32);
    cppOut.fill(0); eskOut.fill(0);
    expectMaterializeSame(d, cppOut.data(), eskOut.data(), cppOut.size(), "uint32 sparse");

    d.sparse_count = 0;
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_NONE;
    d.sparse_indices = nullptr;
    d.sparse_indices_length = 0;
    d.sparse_values = nullptr;
    d.sparse_values_length = 0;
    d.base_data = nullptr;
    d.base_length = 0;
    d.base_stride = 0;
    cppOut.fill(0xff); eskOut.fill(0xff);
    expectMaterializeSame(d, cppOut.data(), eskOut.data(), cppOut.size(), "zero fill");

    d.output.length = 2;
    expectValidationSame(d, "short output");

    d.output.length = cppOut.size();
    d.base_data = base;
    d.base_length = 4;
    d.base_stride = 8;
    expectValidationSame(d, "short base");

    d.base_data = nullptr;
    d.base_length = 0;
    d.base_stride = 0;
    d.sparse_count = 1;
    d.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_UINT16;
    d.sparse_indices = reinterpret_cast<uint8_t *>(sparse16);
    d.sparse_indices_length = 1;
    d.sparse_values = sparseValue;
    d.sparse_values_length = sizeof(sparseValue);
    expectValidationSame(d, "short sparse indices");

    std::cout << "C++ / Eskiu accessor materialization differential: PASS\n";
}
