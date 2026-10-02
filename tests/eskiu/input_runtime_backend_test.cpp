#include "VROEngineInputBackend.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

bool expect(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

VROEngineInputSample makeSample(uint64_t sequence) {
    VROEngineInputSample sample{};
    sample.struct_size = sizeof(sample);
    sample.kind = VRO_ENGINE_INPUT_STYLUS;
    sample.sequence = sequence;
    sample.timestamp_ns = 1000 + sequence;
    sample.frame_id = 2000 + sequence;
    sample.source_id = 7;
    sample.payload.stylus.position = {1.0f, 2.0f, 3.0f};
    sample.payload.stylus.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    sample.payload.stylus.pressure = static_cast<float>(sequence % 100) / 100.0f;
    return sample;
}

bool runBasic(VROEngineBackendPreference preference,
              VROEngineBackendKind expectedBackend) {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_INPUT, preference) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    VROEngineInputQueue *queue = nullptr;
    if (viro_engine_input_queue_create(2, &queue) != VRO_ENGINE_STATUS_OK ||
        queue == nullptr) {
        return false;
    }

    bool ok = true;
    ok &= expect(viro_engine_input_queue_backend(queue) == expectedBackend,
                 "selected backend");
    ok &= expect(viro_engine_input_queue_capacity(queue) == 2, "capacity");

    auto a = makeSample(1);
    auto b = makeSample(2);
    auto c = makeSample(3);
    ok &= expect(viro_engine_input_queue_push(queue, &a) == VRO_ENGINE_INPUT_OK,
                 "push a");
    ok &= expect(viro_engine_input_queue_push(queue, &b) == VRO_ENGINE_INPUT_OK,
                 "push b");
    ok &= expect(viro_engine_input_queue_push(queue, &c) == VRO_ENGINE_INPUT_FULL,
                 "full");
    ok &= expect(viro_engine_input_queue_dropped_count(queue) == 1,
                 "dropped count");

    VROEngineInputSample out{};
    out.struct_size = sizeof(out);
    ok &= expect(viro_engine_input_queue_pop(queue, &out) == VRO_ENGINE_INPUT_OK,
                 "pop a");
    ok &= expect(out.sequence == 1, "fifo a");

    out = {};
    out.struct_size = sizeof(out);
    ok &= expect(viro_engine_input_queue_pop(queue, &out) == VRO_ENGINE_INPUT_OK,
                 "pop b");
    ok &= expect(out.sequence == 2, "fifo b");
    ok &= expect(viro_engine_input_queue_pop(queue, &out) == VRO_ENGINE_INPUT_EMPTY,
                 "empty");

    viro_engine_input_queue_destroy(queue);
    return ok;
}

bool runThreadedEskiuSpsc() {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_INPUT,
            VRO_ENGINE_BACKEND_PREFERENCE_ESKIU) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    VROEngineInputQueue *queue = nullptr;
    if (viro_engine_input_queue_create(64, &queue) != VRO_ENGINE_STATUS_OK ||
        queue == nullptr ||
        viro_engine_input_queue_backend(queue) != VRO_ENGINE_BACKEND_ESKIU) {
        return false;
    }

    constexpr uint64_t kCount = 100000;
    std::atomic<bool> ok{true};

    std::thread producer([&] {
        for (uint64_t sequence = 1; sequence <= kCount; ++sequence) {
            auto sample = makeSample(sequence);
            for (;;) {
                const auto result = viro_engine_input_queue_push(queue, &sample);
                if (result == VRO_ENGINE_INPUT_OK) {
                    break;
                }
                if (result != VRO_ENGINE_INPUT_FULL) {
                    ok.store(false, std::memory_order_relaxed);
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        uint64_t expected = 1;
        VROEngineInputSample out{};
        out.struct_size = sizeof(out);

        while (expected <= kCount) {
            const auto result = viro_engine_input_queue_pop(queue, &out);
            if (result == VRO_ENGINE_INPUT_EMPTY) {
                std::this_thread::yield();
                continue;
            }
            if (result != VRO_ENGINE_INPUT_OK || out.sequence != expected) {
                ok.store(false, std::memory_order_relaxed);
                return;
            }
            ++expected;
        }
    });

    producer.join();
    consumer.join();

    const bool passed =
        ok.load(std::memory_order_relaxed) &&
        viro_engine_input_queue_size(queue) == 0;
    viro_engine_input_queue_destroy(queue);
    return passed;
}

} // namespace

int main() {
    static_assert(sizeof(VROEngineInputSample) == 96,
                  "input ABI must remain 96 bytes");

    bool ok = true;
    ok &= expect(
        (viro_engine_input_backend_availability() &
         VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) != 0,
        "Eskiu backend compiled");

    ok &= expect(
        runBasic(VRO_ENGINE_BACKEND_PREFERENCE_AUTO, VRO_ENGINE_BACKEND_CPP),
        "AUTO remains C++");
    ok &= expect(
        runBasic(VRO_ENGINE_BACKEND_PREFERENCE_CPP, VRO_ENGINE_BACKEND_CPP),
        "explicit C++");
    ok &= expect(
        runBasic(VRO_ENGINE_BACKEND_PREFERENCE_ESKIU, VRO_ENGINE_BACKEND_ESKIU),
        "explicit Eskiu");
    ok &= expect(runThreadedEskiuSpsc(), "threaded Eskiu SPSC");

    viro_engine_backend_preferences_reset();
    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "Eskiu input runtime backend passed\n";
    return EXIT_SUCCESS;
}
