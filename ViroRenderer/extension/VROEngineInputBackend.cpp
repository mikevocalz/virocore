#include "VROEngineInputBackend.h"

#include <new>

#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
extern "C" {
void *viro_eskiu_input_ring_create(uint32_t capacity);
void viro_eskiu_input_ring_destroy(void *ring);
uint32_t viro_eskiu_input_ring_capacity(void *ring);
uint32_t viro_eskiu_input_ring_size(void *ring);
uint64_t viro_eskiu_input_ring_dropped_count(void *ring);
int viro_eskiu_input_ring_push(void *ring, const VROEngineInputSample *sample);
int viro_eskiu_input_ring_pop(void *ring, VROEngineInputSample *sample);
}
#endif

struct VROEngineInputQueue {
    VROEngineBackendKind backend;
    union {
        VROEngineInputRing *cpp_ring;
        void *eskiu_ring;
    } storage;
};

namespace {

VROEngineStatusCode mapCreateResult(VROEngineInputResult result) {
    switch (result) {
        case VRO_ENGINE_INPUT_OK:
            return VRO_ENGINE_STATUS_OK;
        case VRO_ENGINE_INPUT_OUT_OF_MEMORY:
            return VRO_ENGINE_STATUS_OUT_OF_MEMORY;
        case VRO_ENGINE_INPUT_INVALID_ARGUMENT:
            return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
        default:
            return VRO_ENGINE_STATUS_INTERNAL_ERROR;
    }
}

} // namespace

extern "C" uint32_t viro_engine_input_backend_availability(void) {
    uint32_t available = VRO_ENGINE_BACKEND_AVAILABLE_CPP;
#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    available |= VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;
#endif
    return available;
}

extern "C" VROEngineStatusCode viro_engine_input_queue_create(
    uint32_t capacity,
    VROEngineInputQueue **out_queue) {
    if (out_queue == nullptr || capacity == 0) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *out_queue = nullptr;

    const VROEngineBackendKind backend = viro_engine_backend_resolve(
        VRO_ENGINE_DOMAIN_INPUT,
        viro_engine_input_backend_availability());
    if (backend == VRO_ENGINE_BACKEND_UNKNOWN) {
        return VRO_ENGINE_STATUS_UNSUPPORTED;
    }

    VROEngineInputQueue *queue = new (std::nothrow) VROEngineInputQueue{};
    if (queue == nullptr) {
        return VRO_ENGINE_STATUS_OUT_OF_MEMORY;
    }
    queue->backend = backend;

    if (backend == VRO_ENGINE_BACKEND_CPP) {
        VROEngineInputRing *ring = nullptr;
        const VROEngineInputResult result =
            viro_engine_input_ring_create(capacity, &ring);
        if (result != VRO_ENGINE_INPUT_OK) {
            delete queue;
            return mapCreateResult(result);
        }
        queue->storage.cpp_ring = ring;
        *out_queue = queue;
        return VRO_ENGINE_STATUS_OK;
    }

#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    if (backend == VRO_ENGINE_BACKEND_ESKIU) {
        void *ring = viro_eskiu_input_ring_create(capacity);
        if (ring == nullptr) {
            delete queue;
            return VRO_ENGINE_STATUS_OUT_OF_MEMORY;
        }
        queue->storage.eskiu_ring = ring;
        *out_queue = queue;
        return VRO_ENGINE_STATUS_OK;
    }
#endif

    delete queue;
    return VRO_ENGINE_STATUS_UNSUPPORTED;
}

extern "C" void viro_engine_input_queue_destroy(VROEngineInputQueue *queue) {
    if (queue == nullptr) {
        return;
    }

    if (queue->backend == VRO_ENGINE_BACKEND_CPP) {
        viro_engine_input_ring_destroy(queue->storage.cpp_ring);
    }
#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    else if (queue->backend == VRO_ENGINE_BACKEND_ESKIU) {
        viro_eskiu_input_ring_destroy(queue->storage.eskiu_ring);
    }
#endif
    delete queue;
}

extern "C" VROEngineBackendKind viro_engine_input_queue_backend(
    const VROEngineInputQueue *queue) {
    return queue == nullptr ? VRO_ENGINE_BACKEND_UNKNOWN : queue->backend;
}

extern "C" VROEngineInputResult viro_engine_input_queue_push(
    VROEngineInputQueue *queue,
    const VROEngineInputSample *sample) {
    if (queue == nullptr) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }
    if (queue->backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_input_ring_push(queue->storage.cpp_ring, sample);
    }
#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    if (queue->backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineInputResult>(
            viro_eskiu_input_ring_push(queue->storage.eskiu_ring, sample));
    }
#endif
    return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
}

extern "C" VROEngineInputResult viro_engine_input_queue_pop(
    VROEngineInputQueue *queue,
    VROEngineInputSample *out_sample) {
    if (queue == nullptr) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }
    if (queue->backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_input_ring_pop(queue->storage.cpp_ring, out_sample);
    }
#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    if (queue->backend == VRO_ENGINE_BACKEND_ESKIU) {
        return static_cast<VROEngineInputResult>(
            viro_eskiu_input_ring_pop(queue->storage.eskiu_ring, out_sample));
    }
#endif
    return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
}

extern "C" uint32_t viro_engine_input_queue_capacity(
    const VROEngineInputQueue *queue) {
    if (queue == nullptr) {
        return 0;
    }
    if (queue->backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_input_ring_capacity(queue->storage.cpp_ring);
    }
#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    if (queue->backend == VRO_ENGINE_BACKEND_ESKIU) {
        return viro_eskiu_input_ring_capacity(queue->storage.eskiu_ring);
    }
#endif
    return 0;
}

extern "C" uint32_t viro_engine_input_queue_size(
    const VROEngineInputQueue *queue) {
    if (queue == nullptr) {
        return 0;
    }
    if (queue->backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_input_ring_size(queue->storage.cpp_ring);
    }
#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    if (queue->backend == VRO_ENGINE_BACKEND_ESKIU) {
        return viro_eskiu_input_ring_size(queue->storage.eskiu_ring);
    }
#endif
    return 0;
}

extern "C" uint64_t viro_engine_input_queue_dropped_count(
    const VROEngineInputQueue *queue) {
    if (queue == nullptr) {
        return 0;
    }
    if (queue->backend == VRO_ENGINE_BACKEND_CPP) {
        return viro_engine_input_ring_dropped_count(queue->storage.cpp_ring);
    }
#if defined(VRO_ENGINE_ESKIU_INPUT_AVAILABLE) && VRO_ENGINE_ESKIU_INPUT_AVAILABLE
    if (queue->backend == VRO_ENGINE_BACKEND_ESKIU) {
        return viro_eskiu_input_ring_dropped_count(queue->storage.eskiu_ring);
    }
#endif
    return 0;
}
