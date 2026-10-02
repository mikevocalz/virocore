//
// VROEngineInputABI.cpp
//

#include "VROEngineInputABI.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <new>

struct VROEngineInputRing {
    uint32_t capacity;
    VROEngineInputSample *samples;
    std::atomic<uint64_t> write_index;
    std::atomic<uint64_t> read_index;
    std::atomic<uint64_t> dropped_count;
};

namespace {

bool validSample(const VROEngineInputSample *sample) {
    return sample != nullptr &&
           sample->struct_size >= VRO_ENGINE_INPUT_SAMPLE_V0_1_SIZE &&
           sample->kind <= VRO_ENGINE_INPUT_STYLUS;
}

uint64_t ringSize(const VROEngineInputRing *ring) {
    const uint64_t write = ring->write_index.load(std::memory_order_acquire);
    const uint64_t read = ring->read_index.load(std::memory_order_acquire);
    return write >= read ? write - read : 0;
}

} // namespace

extern "C" VROEngineInputResult viro_engine_input_ring_create(
    uint32_t capacity,
    VROEngineInputRing **out_ring) {
    if (out_ring == nullptr || capacity == 0) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }
    *out_ring = nullptr;

    VROEngineInputRing *ring = new (std::nothrow) VROEngineInputRing{};
    if (ring == nullptr) {
        return VRO_ENGINE_INPUT_OUT_OF_MEMORY;
    }

    ring->samples = new (std::nothrow) VROEngineInputSample[capacity]{};
    if (ring->samples == nullptr) {
        delete ring;
        return VRO_ENGINE_INPUT_OUT_OF_MEMORY;
    }

    ring->capacity = capacity;
    ring->write_index.store(0, std::memory_order_relaxed);
    ring->read_index.store(0, std::memory_order_relaxed);
    ring->dropped_count.store(0, std::memory_order_relaxed);
    *out_ring = ring;
    return VRO_ENGINE_INPUT_OK;
}

extern "C" void viro_engine_input_ring_destroy(VROEngineInputRing *ring) {
    if (ring == nullptr) {
        return;
    }
    delete[] ring->samples;
    delete ring;
}

extern "C" VROEngineInputResult viro_engine_input_ring_push(
    VROEngineInputRing *ring,
    const VROEngineInputSample *sample) {
    if (ring == nullptr || !validSample(sample)) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }

    const uint64_t write = ring->write_index.load(std::memory_order_relaxed);
    const uint64_t read = ring->read_index.load(std::memory_order_acquire);
    if (write - read >= ring->capacity) {
        ring->dropped_count.fetch_add(1, std::memory_order_relaxed);
        return VRO_ENGINE_INPUT_FULL;
    }

    VROEngineInputSample normalized{};
    normalized.struct_size = sizeof(normalized);
    const size_t copy_size =
        std::min<size_t>(sample->struct_size, sizeof(normalized));
    std::memcpy(&normalized, sample, copy_size);
    normalized.struct_size = sizeof(normalized);

    ring->samples[write % ring->capacity] = normalized;
    ring->write_index.store(write + 1, std::memory_order_release);
    return VRO_ENGINE_INPUT_OK;
}

extern "C" VROEngineInputResult viro_engine_input_ring_pop(
    VROEngineInputRing *ring,
    VROEngineInputSample *out_sample) {
    if (ring == nullptr || out_sample == nullptr ||
        out_sample->struct_size < VRO_ENGINE_INPUT_SAMPLE_V0_1_SIZE) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }

    const uint64_t read = ring->read_index.load(std::memory_order_relaxed);
    const uint64_t write = ring->write_index.load(std::memory_order_acquire);
    if (read == write) {
        return VRO_ENGINE_INPUT_EMPTY;
    }

    const VROEngineInputSample &sample =
        ring->samples[read % ring->capacity];
    const size_t copy_size =
        std::min<size_t>(out_sample->struct_size, sizeof(sample));
    std::memcpy(out_sample, &sample, copy_size);
    ring->read_index.store(read + 1, std::memory_order_release);
    return VRO_ENGINE_INPUT_OK;
}

extern "C" uint32_t viro_engine_input_ring_capacity(
    const VROEngineInputRing *ring) {
    return ring == nullptr ? 0 : ring->capacity;
}

extern "C" uint32_t viro_engine_input_ring_size(
    const VROEngineInputRing *ring) {
    if (ring == nullptr) {
        return 0;
    }
    const uint64_t size = ringSize(ring);
    return size > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(size);
}

extern "C" uint64_t viro_engine_input_ring_dropped_count(
    const VROEngineInputRing *ring) {
    return ring == nullptr
        ? 0
        : ring->dropped_count.load(std::memory_order_relaxed);
}
