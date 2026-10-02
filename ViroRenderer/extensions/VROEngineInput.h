#pragma once

#include "VROEngineContract.h"

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VROEngineInputKind {
    VRO_ENGINE_INPUT_CONTROLLER = 0,
    VRO_ENGINE_INPUT_HAND = 1,
    VRO_ENGINE_INPUT_GAZE = 2,
    VRO_ENGINE_INPUT_STYLUS = 3,
    VRO_ENGINE_INPUT_TOUCH = 4,
    VRO_ENGINE_INPUT_MOUSE = 5
} VROEngineInputKind;

typedef struct VROEnginePose {
    float position[3];
    float orientation[4];
} VROEnginePose;

typedef struct VROEngineInputSample {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t source_id;
    uint32_t flags;
    uint64_t timestamp_ns;
    VROEnginePose pose;
    float analog[4];
    uint64_t buttons;
} VROEngineInputSample;

typedef struct VROEngineInputBatch {
    uint32_t struct_size;
    uint32_t count;
    const VROEngineInputSample *samples;
} VROEngineInputBatch;

VROEngineStatus vro_engine_validate_input_batch(const VROEngineInputBatch *batch);

#ifdef __cplusplus
}
#endif
