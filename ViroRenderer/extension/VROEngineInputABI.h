//
// VROEngineInputABI.h
//
// Bounded language-neutral input sample ABI.
// Production adapters are intentionally separate from this storage contract.
//

#ifndef VRO_ENGINE_INPUT_ABI_H
#define VRO_ENGINE_INPUT_ABI_H

#include <stdint.h>
#include "VROEngineABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_INPUT_ABI_MAJOR 0u
#define VRO_ENGINE_INPUT_ABI_MINOR 1u

typedef enum VROEngineInputKind {
    VRO_ENGINE_INPUT_UNKNOWN = 0,
    VRO_ENGINE_INPUT_CONTROLLER = 1,
    VRO_ENGINE_INPUT_HAND_JOINT = 2,
    VRO_ENGINE_INPUT_GAZE = 3,
    VRO_ENGINE_INPUT_STYLUS = 4
} VROEngineInputKind;

typedef enum VROEngineInputResult {
    VRO_ENGINE_INPUT_OK = 0,
    VRO_ENGINE_INPUT_EMPTY = 1,
    VRO_ENGINE_INPUT_FULL = 2,
    VRO_ENGINE_INPUT_INVALID_ARGUMENT = 3,
    VRO_ENGINE_INPUT_OUT_OF_MEMORY = 4
} VROEngineInputResult;

typedef struct VROEngineControllerInput {
    VROEngineVec3 position;
    VROEngineQuat orientation;
    uint32_t buttons;
    float axes[4];
    float trigger;
    float grip;
} VROEngineControllerInput;

typedef struct VROEngineHandJointInput {
    VROEngineVec3 position;
    VROEngineQuat orientation;
    uint32_t joint_index;
    float radius;
    float confidence;
} VROEngineHandJointInput;

typedef struct VROEngineGazeInput {
    VROEngineVec3 origin;
    VROEngineVec3 direction;
    float confidence;
} VROEngineGazeInput;

typedef struct VROEngineStylusInput {
    VROEngineVec3 position;
    VROEngineQuat orientation;
    float pressure;
    float tilt_x;
    float tilt_y;
    uint32_t buttons;
} VROEngineStylusInput;

typedef union VROEngineInputPayload {
    VROEngineControllerInput controller;
    VROEngineHandJointInput hand_joint;
    VROEngineGazeInput gaze;
    VROEngineStylusInput stylus;
} VROEngineInputPayload;

typedef struct VROEngineInputSample {
    uint32_t struct_size;
    uint32_t kind;
    uint64_t sequence;
    uint64_t timestamp_ns;
    uint64_t frame_id;
    uint32_t source_id;
    uint32_t flags;
    VROEngineInputPayload payload;
} VROEngineInputSample;

#define VRO_ENGINE_INPUT_SAMPLE_V0_1_SIZE ((uint32_t)sizeof(VROEngineInputSample))

typedef struct VROEngineInputRing VROEngineInputRing;

/*
 * Creates a single-producer / single-consumer ring with fixed capacity.
 * Allocation happens only here. push/pop allocate nothing.
 */
VROEngineInputResult viro_engine_input_ring_create(
    uint32_t capacity,
    VROEngineInputRing **out_ring);

void viro_engine_input_ring_destroy(VROEngineInputRing *ring);

/*
 * Producer-thread only. Returns FULL without overwriting existing samples when
 * the ring is saturated; dropped_count is incremented.
 */
VROEngineInputResult viro_engine_input_ring_push(
    VROEngineInputRing *ring,
    const VROEngineInputSample *sample);

/*
 * Consumer-thread only. Caller sets out_sample->struct_size.
 */
VROEngineInputResult viro_engine_input_ring_pop(
    VROEngineInputRing *ring,
    VROEngineInputSample *out_sample);

uint32_t viro_engine_input_ring_capacity(const VROEngineInputRing *ring);
uint32_t viro_engine_input_ring_size(const VROEngineInputRing *ring);
uint64_t viro_engine_input_ring_dropped_count(const VROEngineInputRing *ring);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(VROEngineVec3) == 12, "Vec3 ABI changed");
static_assert(sizeof(VROEngineQuat) == 16, "Quat ABI changed");
static_assert(sizeof(VROEngineInputSample) == 96, "Input sample ABI v0.1 must stay 96 bytes");
#endif

#endif // VRO_ENGINE_INPUT_ABI_H
