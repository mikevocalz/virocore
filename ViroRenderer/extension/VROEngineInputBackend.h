#ifndef VRO_ENGINE_INPUT_BACKEND_H
#define VRO_ENGINE_INPUT_BACKEND_H

#include <stdint.h>

#include "VROEngineBackendSelector.h"
#include "VROEngineInputABI.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime-selectable input storage facade.
 *
 * Existing viro_engine_input_ring_* functions remain the C++ reference ABI.
 * This facade resolves the INPUT domain through VROEngineBackendSelector and
 * creates either the C++ reference ring or the Eskiu implementation when that
 * implementation is compiled into the binary.
 */
typedef struct VROEngineInputQueue VROEngineInputQueue;

uint32_t viro_engine_input_backend_availability(void);

VROEngineStatusCode viro_engine_input_queue_create(
    uint32_t capacity,
    VROEngineInputQueue **out_queue);

void viro_engine_input_queue_destroy(VROEngineInputQueue *queue);

VROEngineBackendKind viro_engine_input_queue_backend(
    const VROEngineInputQueue *queue);

VROEngineInputResult viro_engine_input_queue_push(
    VROEngineInputQueue *queue,
    const VROEngineInputSample *sample);

VROEngineInputResult viro_engine_input_queue_pop(
    VROEngineInputQueue *queue,
    VROEngineInputSample *out_sample);

uint32_t viro_engine_input_queue_capacity(
    const VROEngineInputQueue *queue);

uint32_t viro_engine_input_queue_size(
    const VROEngineInputQueue *queue);

uint64_t viro_engine_input_queue_dropped_count(
    const VROEngineInputQueue *queue);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VRO_ENGINE_INPUT_BACKEND_H
