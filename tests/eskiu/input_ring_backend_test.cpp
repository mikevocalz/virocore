#include "VROEngineInputABI.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern "C" {
void *viro_eskiu_input_ring_create(uint32_t capacity);
void viro_eskiu_input_ring_destroy(void *ring);
uint32_t viro_eskiu_input_ring_capacity(void *ring);
uint32_t viro_eskiu_input_ring_size(void *ring);
uint64_t viro_eskiu_input_ring_dropped_count(void *ring);
int viro_eskiu_input_ring_push(void *ring, const VROEngineInputSample *sample);
int viro_eskiu_input_ring_pop(void *ring, VROEngineInputSample *sample);
}

static bool expect(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

static VROEngineInputSample sample(uint64_t sequence, float pressure) {
    VROEngineInputSample s{};
    s.struct_size = sizeof(s);
    s.kind = VRO_ENGINE_INPUT_STYLUS;
    s.sequence = sequence;
    s.timestamp_ns = 1000 + sequence;
    s.frame_id = 2000 + sequence;
    s.source_id = 7;
    s.payload.stylus.position = {1.0f, 2.0f, 3.0f};
    s.payload.stylus.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    s.payload.stylus.pressure = pressure;
    return s;
}

int main() {
    static_assert(sizeof(VROEngineInputSample) == 96, "ABI must remain 96 bytes");
    bool ok = true;
    void *ring = viro_eskiu_input_ring_create(2);
    ok &= expect(ring != nullptr, "create");
    ok &= expect(viro_eskiu_input_ring_capacity(ring) == 2, "capacity");

    auto a = sample(1, 0.4f);
    auto b = sample(2, 0.8f);
    auto c = sample(3, 1.0f);
    ok &= expect(viro_eskiu_input_ring_push(ring, &a) == VRO_ENGINE_INPUT_OK, "push a");
    ok &= expect(viro_eskiu_input_ring_push(ring, &b) == VRO_ENGINE_INPUT_OK, "push b");
    ok &= expect(viro_eskiu_input_ring_push(ring, &c) == VRO_ENGINE_INPUT_FULL, "full");
    ok &= expect(viro_eskiu_input_ring_dropped_count(ring) == 1, "dropped");

    VROEngineInputSample out{};
    out.struct_size = sizeof(out);
    ok &= expect(viro_eskiu_input_ring_pop(ring, &out) == VRO_ENGINE_INPUT_OK, "pop a");
    ok &= expect(out.sequence == 1 && out.payload.stylus.pressure == 0.4f, "a preserved");

    std::memset(&out, 0, sizeof(out));
    out.struct_size = sizeof(out);
    ok &= expect(viro_eskiu_input_ring_pop(ring, &out) == VRO_ENGINE_INPUT_OK, "pop b");
    ok &= expect(out.sequence == 2 && out.payload.stylus.pressure == 0.8f, "b preserved");
    ok &= expect(viro_eskiu_input_ring_pop(ring, &out) == VRO_ENGINE_INPUT_EMPTY, "empty");

    viro_eskiu_input_ring_destroy(ring);
    if (!ok) return EXIT_FAILURE;
    std::cout << "Eskiu input ring shadow backend passed\n";
    return EXIT_SUCCESS;
}
