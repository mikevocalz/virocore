#pragma once
#include <cmath>

enum class VROHandPinchEdge { None, Down, Up, Cancel };

// Which measurement produced the current press. A press latches the metric
// that opened it; samples from the other metric are treated as invalid
// until the press ends. Nothing is latched while released.
enum class VROHandPinchMetric { None, Strength, Distance };

class VROHandPinchState {
public:
    // Joint-distance metric (thumb tip to index tip). Candidate 20 mm press /
    // 25 mm release; not device qualified.
    VROHandPinchEdge updateDistance(bool active, float metres) {
        const bool valid = active && std::isfinite(metres) && metres >= 0;
        return updateThresholds(VROHandPinchMetric::Distance, valid,
                                metres <= 0.020f, metres >= 0.025f);
    }

    // FB aim pinch-strength metric. Candidate 0.75 press / 0.55 release; not
    // device qualified.
    VROHandPinchEdge update(bool active, float strength) {
        const bool valid = active && std::isfinite(strength);
        return updateThresholds(VROHandPinchMetric::Strength, valid,
                                strength >= 0.75f, strength <= 0.55f);
    }

    // Immediate cancel with no grace: system gesture, menu press, controller
    // takeover, hand tracking disabled, tracker destroyed. Emits Cancel once
    // if a press was open, then disarms so the next press needs a released
    // sample first.
    VROHandPinchEdge cancel() {
        const bool wasPressed = pressed;
        resetToDisarmed();
        return wasPressed ? VROHandPinchEdge::Cancel : VROHandPinchEdge::None;
    }

    bool isPressed() const { return pressed; }
    bool isArmed() const { return armed; }
    VROHandPinchMetric latchedMetric() const { return metric; }

private:
    VROHandPinchEdge updateThresholds(VROHandPinchMetric sampleMetric, bool valid,
                                      bool press, bool release) {
        if (pressed) {
            // Locked to the metric that opened the press. A sample from the
            // other metric is a dropout, never a reading.
            if (sampleMetric != metric) valid = false;
            if (!valid) {
                resetToDisarmed();
                return VROHandPinchEdge::Cancel;
            }
            if (release) {
                pressed = false;
                metric = VROHandPinchMetric::None;
                return VROHandPinchEdge::Up;
            }
            return VROHandPinchEdge::None;
        }

        // Released. The metric may change freely, but a new metric must show
        // a released sample before it can arm, same as the first sample ever.
        if (sampleMetric != lastMetric) {
            armed = false;
            lastMetric = sampleMetric;
        }
        if (!valid) {
            armed = false;
            return VROHandPinchEdge::None;
        }
        if (!armed) {
            if (release) armed = true;
            return VROHandPinchEdge::None;
        }
        if (press) {
            pressed = true;
            metric = sampleMetric;
            return VROHandPinchEdge::Down;
        }
        return VROHandPinchEdge::None;
    }

    void resetToDisarmed() {
        pressed = false;
        armed = false;
        metric = VROHandPinchMetric::None;
        lastMetric = VROHandPinchMetric::None;
    }

    bool pressed = false;
    // Disarmed at start. A hand that is already pinching when tracking begins
    // (or resumes, or is re-enabled) must open before it can click.
    bool armed = false;
    VROHandPinchMetric metric = VROHandPinchMetric::None;      // latched by the open press
    VROHandPinchMetric lastMetric = VROHandPinchMetric::None;  // last metric sampled while released
};

inline bool VROHandDirectionIsValid(float x, float y, float z) {
    const float squared = x*x + y*y + z*z;
    return std::isfinite(squared) && squared > 1e-12f;
}
