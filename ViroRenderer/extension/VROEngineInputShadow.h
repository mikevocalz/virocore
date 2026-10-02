//
// VROEngineInputShadow.h
//
// C++ adapter from existing Viro math/input values into the language-neutral
// bounded input ABI. This does not dispatch or replace production input events.
//

#ifndef VRO_ENGINE_INPUT_SHADOW_H
#define VRO_ENGINE_INPUT_SHADOW_H

#include <stdint.h>

#include "VROEngineInputABI.h"
#include "VROQuaternion.h"
#include "VROVector3f.h"

class VROEngineInputShadow {
public:
    explicit VROEngineInputShadow(uint32_t capacity);
    ~VROEngineInputShadow();

    VROEngineInputShadow(const VROEngineInputShadow &) = delete;
    VROEngineInputShadow &operator=(const VROEngineInputShadow &) = delete;

    bool isReady() const;

    VROEngineInputResult mirrorController(
        uint32_t sourceId,
        uint64_t timestampNs,
        uint64_t frameId,
        const VROVector3f &position,
        const VROQuaternion &orientation,
        uint32_t buttons = 0,
        const float axes[4] = nullptr,
        float trigger = 0.0f,
        float grip = 0.0f);

    VROEngineInputResult mirrorStylus(
        uint32_t sourceId,
        uint64_t timestampNs,
        uint64_t frameId,
        const VROVector3f &position,
        const VROQuaternion &orientation,
        float pressure,
        float tiltX,
        float tiltY,
        uint32_t buttons);

    VROEngineInputResult mirrorGaze(
        uint32_t sourceId,
        uint64_t timestampNs,
        uint64_t frameId,
        const VROVector3f &origin,
        const VROVector3f &direction,
        float confidence);

    VROEngineInputResult mirrorHandJoint(
        uint32_t sourceId,
        uint64_t timestampNs,
        uint64_t frameId,
        uint32_t jointIndex,
        const VROVector3f &position,
        const VROQuaternion &orientation,
        float radius,
        float confidence);

    VROEngineInputResult pop(VROEngineInputSample *outSample);
    uint32_t size() const;
    uint64_t droppedCount() const;

private:
    VROEngineInputSample makeBase(
        VROEngineInputKind kind,
        uint32_t sourceId,
        uint64_t timestampNs,
        uint64_t frameId);

    VROEngineInputRing *_ring;
    uint64_t _nextSequence;
};

#endif // VRO_ENGINE_INPUT_SHADOW_H
