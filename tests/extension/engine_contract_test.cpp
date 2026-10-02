#include "VROEngineABI.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok &= expect(viro_engine_query_abi(nullptr) == VRO_ENGINE_STATUS_INVALID_ARGUMENT,
                 "null ABI info must be rejected");

    VROEngineAbiInfo too_small{};
    too_small.struct_size = static_cast<uint32_t>(sizeof(VROEngineAbiInfo) - 1);
    too_small.backend_kind = 0xA5A5A5A5u;
    ok &= expect(viro_engine_query_abi(&too_small) == VRO_ENGINE_STATUS_INVALID_ARGUMENT,
                 "undersized ABI info must be rejected");
    ok &= expect(too_small.backend_kind == 0xA5A5A5A5u,
                 "rejected query must not partially overwrite caller storage");

    VROEngineAbiInfo exact{};
    exact.struct_size = static_cast<uint32_t>(sizeof(exact));
    ok &= expect(viro_engine_query_abi(&exact) == VRO_ENGINE_STATUS_OK,
                 "exact-size ABI query must succeed");
    ok &= expect(exact.struct_size == sizeof(VROEngineAbiInfo),
                 "returned struct size must describe the implementation layout");
    ok &= expect(exact.abi_major == VRO_ENGINE_ABI_VERSION_MAJOR,
                 "ABI major must match the header");
    ok &= expect(exact.abi_minor == VRO_ENGINE_ABI_VERSION_MINOR,
                 "ABI minor must match the header");
    ok &= expect(exact.backend_kind == VRO_ENGINE_BACKEND_CPP,
                 "reference backend must identify as C++");
    ok &= expect(exact.capabilities == 0,
                 "scaffold must not advertise unwired production capabilities");

    struct ExtendedInfo {
        VROEngineAbiInfo info;
        uint64_t future_tail;
    } extended{};
    constexpr uint64_t kSentinel = UINT64_C(0x1122334455667788);
    extended.info.struct_size = static_cast<uint32_t>(sizeof(extended));
    extended.future_tail = kSentinel;
    ok &= expect(viro_engine_query_abi(&extended.info) == VRO_ENGINE_STATUS_OK,
                 "newer/larger callers must be accepted");
    ok &= expect(extended.info.struct_size == sizeof(VROEngineAbiInfo),
                 "implementation must report only the bytes it understands");
    ok &= expect(extended.future_tail == kSentinel,
                 "unknown future caller tail must remain untouched");

    const uint32_t packed = viro_engine_abi_version();
    const uint32_t expected =
        (static_cast<uint32_t>(VRO_ENGINE_ABI_VERSION_MAJOR) << 16u) |
        static_cast<uint32_t>(VRO_ENGINE_ABI_VERSION_MINOR);
    ok &= expect(packed == expected, "packed ABI version must match header constants");

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "Viro engine contract reference runtime passed\n";
    return EXIT_SUCCESS;
}
