//
// VROEngineInputShadow.cpp
//

#include "VROEngineInputShadow.h"

namespace {

VROEngineVec3 toAbiVec3(const VROVector3f &value) {
    return { value.x, value.y, value.z };
}

VROEngineQuat toAbiQuat(const VROQuaternion &value) {
    return { value.X, value.Y, value.Z, value.W };
}

} // namespace

VROEngineInputShadow::VROEngineInputShadow(uint32_t capacity)
    : _ring(nullptr), _nextSequence(1) {
    viro_engine_input_ring_create(capacity, &_ring);
}

VROEngineInputShadow::~VROEngineInputShadow() {
    viro_engine_input_ring_destroy(_ring);
}

bool VROEngineInputShadow::isReady() const {
    return _ring != nullptr;
}

VROEngineInputSample VROEngineInputShadow::makeBase(
    VROEngineInputKind kind,
    uint32_t sourceId,
    uint64_t timestampNs,
    uint64_t frameId) {
    VROEngineInputSample sample{};
    sample.struct_size = sizeof(sample);
    sample.kind = static_cast<uint32_t>(kind);
    sample.sequence = _nextSequence++;
    sample.timestamp_ns = timestampNs;
    sample.frame_id = frameId;
    sample.source_id = sourceId;
    return sample;
}

VROEngineInputResult VROEngineInputShadow::mirrorController(
    uint32_t sourceId,
    uint64_t timestampNs,
    uint64_t frameId,
    const VROVector3f &position,
    const VROQuaternion &orientation,
    uint32_t buttons,
    const float axes[4],
    float trigger,
    float grip) {
    if (_ring == nullptr) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }

    auto sample = makeBase(
        VRO_ENGINE_INPUT_CONTROLLER, sourceId, timestampNs, frameId);
    sample.payload.controller.position = toAbiVec3(position);
    sample.payload.controller.orientation = toAbiQuat(orientation);
    sample.payload.controller.buttons = buttons;
    if (axes != nullptr) {
        for (int i = 0; i < 4; ++i) {
            sample.payload.controller.axes[i] = axes[i];
        }
    }
    sample.payload.controller.trigger = trigger;
    sample.payload.controller.grip = grip;
    return viro_engine_input_ring_push(_ring, &sample);
}

VROEngineInputResult VROEngineInputShadow::mirrorStylus(
    uint32_t sourceId,
    uint64_t timestampNs,
    uint64_t frameId,
    const VROVector3f &position,
    const VROQuaternion &orientation,
    float pressure,
    float tiltX,
    float tiltY,
    uint32_t buttons) {
    if (_ring == nullptr) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }

    auto sample = makeBase(
        VRO_ENGINE_INPUT_STYLUS, sourceId, timestampNs, frameId);
    sample.payload.stylus.position = toAbiVec3(position);
    sample.payload.stylus.orientation = toAbiQuat(orientation);
    sample.payload.stylus.pressure = pressure;
    sample.payload.stylus.tilt_x = tiltX;
    sample.payload.stylus.tilt_y = tiltY;
    sample.payload.stylus.buttons = buttons;
    return viro_engine_input_ring_push(_ring, &sample);
}

VROEngineInputResult VROEngineInputShadow::mirrorGaze(
    uint32_t sourceId,
    uint64_t timestampNs,
    uint64_t frameId,
    const VROVector3f &origin,
    const VROVector3f &direction,
    float confidence) {
    if (_ring == nullptr) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }

    auto sample = makeBase(
        VRO_ENGINE_INPUT_GAZE, sourceId, timestampNs, frameId);
    sample.payload.gaze.origin = toAbiVec3(origin);
    sample.payload.gaze.direction = toAbiVec3(direction);
    sample.payload.gaze.confidence = confidence;
    return viro_engine_input_ring_push(_ring, &sample);
}

VROEngineInputResult VROEngineInputShadow::mirrorHandJoint(
    uint32_t sourceId,
    uint64_t timestampNs,
    uint64_t frameId,
    uint32_t jointIndex,
    const VROVector3f &position,
    const VROQuaternion &orientation,
    float radius,
    float confidence) {
    if (_ring == nullptr) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }

    auto sample = makeBase(
        VRO_ENGINE_INPUT_HAND_JOINT, sourceId, timestampNs, frameId);
    sample.payload.hand_joint.position = toAbiVec3(position);
    sample.payload.hand_joint.orientation = toAbiQuat(orientation);
    sample.payload.hand_joint.joint_index = jointIndex;
    sample.payload.hand_joint.radius = radius;
    sample.payload.hand_joint.confidence = confidence;
    return viro_engine_input_ring_push(_ring, &sample);
}

VROEngineInputResult VROEngineInputShadow::pop(
    VROEngineInputSample *outSample) {
    if (_ring == nullptr) {
        return VRO_ENGINE_INPUT_INVALID_ARGUMENT;
    }
    return viro_engine_input_ring_pop(_ring, outSample);
}

uint32_t VROEngineInputShadow::size() const {
    return viro_engine_input_ring_size(_ring);
}

uint64_t VROEngineInputShadow::droppedCount() const {
    return viro_engine_input_ring_dropped_count(_ring);
}
