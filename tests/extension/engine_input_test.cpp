#include "VROEngineInputABI.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

bool expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

VROEngineInputSample makeStylus(uint64_t sequence, float pressure) {
    VROEngineInputSample sample{};
    sample.struct_size = sizeof(sample);
    sample.kind = VRO_ENGINE_INPUT_STYLUS;
    sample.sequence = sequence;
    sample.timestamp_ns = 1000 + sequence;
    sample.frame_id = 2000 + sequence;
    sample.source_id = 7;
    sample.payload.stylus.position = {1.0f, 2.0f, 3.0f};
    sample.payload.stylus.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    sample.payload.stylus.pressure = pressure;
    sample.payload.stylus.tilt_x = 0.25f;
    sample.payload.stylus.tilt_y = -0.5f;
    sample.payload.stylus.buttons = 3;
    return sample;
}

} // namespace

int main() {
    bool ok = true;

    VROEngineInputRing *ring = nullptr;
    ok &= expect(viro_engine_input_ring_create(2, &ring) == VRO_ENGINE_INPUT_OK,
                 "create ring");
    ok &= expect(ring != nullptr, "ring exists");
    ok &= expect(viro_engine_input_ring_capacity(ring) == 2, "capacity");
    ok &= expect(viro_engine_input_ring_size(ring) == 0, "starts empty");

    auto one = makeStylus(1, 0.4f);
    auto two = makeStylus(2, 0.8f);
    auto three = makeStylus(3, 1.0f);

    ok &= expect(viro_engine_input_ring_push(ring, &one) == VRO_ENGINE_INPUT_OK,
                 "push first");
    ok &= expect(viro_engine_input_ring_push(ring, &two) == VRO_ENGINE_INPUT_OK,
                 "push second");
    ok &= expect(viro_engine_input_ring_size(ring) == 2, "ring full");
    ok &= expect(viro_engine_input_ring_push(ring, &three) == VRO_ENGINE_INPUT_FULL,
                 "saturation returns full");
    ok &= expect(viro_engine_input_ring_dropped_count(ring) == 1,
                 "saturation increments dropped count");

    VROEngineInputSample out{};
    out.struct_size = sizeof(out);
    ok &= expect(viro_engine_input_ring_pop(ring, &out) == VRO_ENGINE_INPUT_OK,
                 "pop first");
    ok &= expect(out.sequence == 1, "FIFO order first");
    ok &= expect(out.kind == VRO_ENGINE_INPUT_STYLUS, "kind preserved");
    ok &= expect(out.payload.stylus.pressure == 0.4f, "payload preserved");

    std::memset(&out, 0, sizeof(out));
    out.struct_size = sizeof(out);
    ok &= expect(viro_engine_input_ring_pop(ring, &out) == VRO_ENGINE_INPUT_OK,
                 "pop second");
    ok &= expect(out.sequence == 2, "FIFO order second");
    ok &= expect(viro_engine_input_ring_pop(ring, &out) == VRO_ENGINE_INPUT_EMPTY,
                 "empty result");

    // Space becomes available again after the consumer advances.
    ok &= expect(viro_engine_input_ring_push(ring, &three) == VRO_ENGINE_INPUT_OK,
                 "push after pop");

    VROEngineInputSample invalid{};
    invalid.struct_size = sizeof(uint32_t);
    ok &= expect(viro_engine_input_ring_push(ring, &invalid) ==
                     VRO_ENGINE_INPUT_INVALID_ARGUMENT,
                 "undersized sample rejected");

    viro_engine_input_ring_destroy(ring);
    viro_engine_input_ring_destroy(nullptr);

    if (!ok) return EXIT_FAILURE;
    std::cout << "Viro engine input ABI passed\n";
    return EXIT_SUCCESS;
}
