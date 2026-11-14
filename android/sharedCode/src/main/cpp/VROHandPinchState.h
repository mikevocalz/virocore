#pragma once
#include <cmath>

enum class VROHandPinchEdge { None, Down, Up, Cancel };

class VROHandPinchState {
public:
    VROHandPinchEdge updateDistance(bool active, float metres) {
        // Candidate 20 mm press / 25 mm release; not device qualified.
        return updateThresholds(active && std::isfinite(metres) && metres >= 0,
                                metres <= 0.020f, metres >= 0.025f);
    }
    // Candidate thresholds; device qualification is required before release.
    VROHandPinchEdge update(bool active, float strength) {
        return updateThresholds(active && std::isfinite(strength),
                                strength >= 0.75f, strength <= 0.55f);
    }
private:
    VROHandPinchEdge updateThresholds(bool active, bool press, bool release) {
        if (!active) {
            const bool wasPressed = pressed;
            pressed = false;
            armed = false;
            return wasPressed ? VROHandPinchEdge::Cancel : VROHandPinchEdge::None;
        }
        if (!armed) {
            if (release) armed = true;
            return VROHandPinchEdge::None;
        }
        if (!pressed && press) {
            pressed = true;
            return VROHandPinchEdge::Down;
        }
        if (pressed && release) {
            pressed = false;
            return VROHandPinchEdge::Up;
        }
        return VROHandPinchEdge::None;
    }
private:
    bool pressed = false;
    bool armed = true;
};

inline bool VROHandDirectionIsValid(float x, float y, float z) {
    const float squared = x*x + y*y + z*z;
    return std::isfinite(squared) && squared > 1e-12f;
}
