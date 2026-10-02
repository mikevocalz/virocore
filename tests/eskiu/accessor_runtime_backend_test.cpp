#include "VROEngineAccessorBackend.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

bool expect(bool ok, const char *label) {
    if (!ok) {
        std::cerr << "FAIL: " << label << "\n";
        return false;
    }
    return true;
}

bool runBackend(
    VROEngineBackendPreference preference,
    VROEngineBackendKind expectedBackend) {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_ASSET_LOADING,
            preference) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    bool ok = expect(
        viro_engine_accessor_backend_resolve() == expectedBackend,
        "accessor backend");

    uint8_t base[] = {
        1,2,3,4, 99,99,99,99,
        5,6,7,8, 99,99,99,99,
        9,10,11,12, 99,99,99,99
    };
    uint16_t sparseIndex[] = {1};
    uint8_t sparseValue[] = {40,41,42,43};
    std::array<uint8_t, 12> out{};

    VROEngineAccessorMaterializeDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.element_size = 4;
    desc.element_count = 3;
    desc.base_data = base;
    desc.base_length = sizeof(base);
    desc.base_stride = 8;
    desc.sparse_index_type = VRO_ENGINE_SPARSE_INDEX_UINT16;
    desc.sparse_count = 1;
    desc.sparse_indices =
        reinterpret_cast<const uint8_t *>(sparseIndex);
    desc.sparse_indices_length = sizeof(sparseIndex);
    desc.sparse_values = sparseValue;
    desc.sparse_values_length = sizeof(sparseValue);
    desc.output = {out.data(), out.size()};

    ok &= expect(
        viro_engine_accessor_materialize_validate_selected(&desc) ==
            VRO_ENGINE_STATUS_OK,
        "accessor validation");
    ok &= expect(
        viro_engine_accessor_materialize_selected(&desc) ==
            VRO_ENGINE_STATUS_OK,
        "accessor materialize");

    const uint8_t expected[] = {
        1,2,3,4,
        40,41,42,43,
        9,10,11,12
    };
    ok &= expect(
        std::memcmp(out.data(), expected, sizeof(expected)) == 0,
        "strided base plus sparse patch");

    desc.output.length = 2;
    ok &= expect(
        viro_engine_accessor_materialize_selected(&desc) ==
            VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "short output rejected");

    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= expect(
        (viro_engine_accessor_backend_availability() &
         VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) != 0,
        "Eskiu accessor compiled");
    ok &= expect(
        runBackend(VRO_ENGINE_BACKEND_PREFERENCE_AUTO,
                   VRO_ENGINE_BACKEND_CPP),
        "AUTO remains C++");
    ok &= expect(
        runBackend(VRO_ENGINE_BACKEND_PREFERENCE_CPP,
                   VRO_ENGINE_BACKEND_CPP),
        "explicit C++");
    ok &= expect(
        runBackend(VRO_ENGINE_BACKEND_PREFERENCE_ESKIU,
                   VRO_ENGINE_BACKEND_ESKIU),
        "explicit Eskiu");

    viro_engine_backend_preferences_reset();
    if (!ok) return EXIT_FAILURE;
    std::cout << "Eskiu accessor runtime backend passed\n";
    return EXIT_SUCCESS;
}
