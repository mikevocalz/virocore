// Host-only test for VROHandPinchState. Run via tools/run-host-tests.sh, or:
//   c++ -std=c++14 -Wall -Wextra -Werror -Iandroid/sharedCode/src/main/cpp \
//     android/sharedCode/src/test/cpp/VROHandPinchStateTest.cpp -o /tmp/pinch-test \
//     && /tmp/pinch-test
#include "VROHandPinchState.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

// assert() vanishes under NDEBUG. CHECK always evaluates and exits non-zero.
static int g_failures = 0;
#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

using Edge = VROHandPinchEdge;
using Metric = VROHandPinchMetric;
static const float kInf = std::numeric_limits<float>::infinity();
static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const int kGrace = VROHandPinchState::kInvalidSampleGraceFrames;

static void directionValidity() {
    CHECK(VROHandDirectionIsValid(0, 0, -1));
    CHECK(!VROHandDirectionIsValid(0, 0, 0));
    CHECK(!VROHandDirectionIsValid(kInf, 0, 0));
    CHECK(!VROHandDirectionIsValid(kNaN, 0, 0));
}

// Fix 2: the very first sample never produces a Down. A hand that is already
// pinching when tracking starts must open first.
static void firstSampleAlreadyPinched() {
    VROHandPinchState s;
    CHECK(!s.isArmed());
    CHECK(s.update(true, 1.0f) == Edge::None);
    CHECK(s.update(true, 0.9f) == Edge::None);
    CHECK(!s.isArmed());
    CHECK(s.update(true, 0.3f) == Edge::None);  // released: arms
    CHECK(s.isArmed());
    CHECK(s.update(true, 0.8f) == Edge::Down);

    VROHandPinchState d;
    CHECK(d.updateDistance(true, 0.010f) == Edge::None);
    CHECK(d.updateDistance(true, 0.030f) == Edge::None);
    CHECK(d.updateDistance(true, 0.010f) == Edge::Down);
}

// Fix 2: after a cancel (tracking lost, disabled, system gesture) the state
// is disarmed again and needs a released sample before the next Down.
static void rearmAfterCancel() {
    VROHandPinchState s;
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, 0.9f) == Edge::Down);
    CHECK(s.cancel() == Edge::Cancel);
    CHECK(!s.isPressed());
    CHECK(!s.isArmed());
    CHECK(s.update(true, 0.9f) == Edge::None);   // still pinched: no Down
    CHECK(s.update(true, 0.2f) == Edge::None);   // open: arms
    CHECK(s.update(true, 0.9f) == Edge::Down);
}

// Basic hysteresis on each metric (thresholds are candidates, not qualified).
static void hysteresis() {
    VROHandPinchState s;
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, 0.75f) == Edge::Down);
    CHECK(s.update(true, 0.65f) == Edge::None);
    CHECK(s.update(true, 0.56f) == Edge::None);
    CHECK(s.update(true, 0.55f) == Edge::Up);
    CHECK(s.update(true, 0.74f) == Edge::None);
    CHECK(s.update(true, 0.75f) == Edge::Down);

    VROHandPinchState d;
    CHECK(d.updateDistance(true, 0.030f) == Edge::None);
    CHECK(d.updateDistance(true, 0.020f) == Edge::Down);
    CHECK(d.updateDistance(true, 0.023f) == Edge::None);
    CHECK(d.updateDistance(true, 0.025f) == Edge::Up);
    CHECK(d.updateDistance(true, 0.019f) == Edge::Down);
}

// Exact threshold edges. 0.7499 is below press; 0.5499 is below release.
static void thresholdEdges() {
    VROHandPinchState s;
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, 0.7499f) == Edge::None);
    CHECK(!s.isPressed());
    CHECK(s.update(true, 0.75f) == Edge::Down);
    CHECK(s.update(true, 0.5501f) == Edge::None);
    CHECK(s.isPressed());
    CHECK(s.update(true, 0.5499f) == Edge::Up);
    CHECK(!s.isPressed());
    // Dead band between release and press does nothing in either state.
    CHECK(s.update(true, 0.60f) == Edge::None);
    CHECK(s.update(true, 0.75f) == Edge::Down);
    CHECK(s.update(true, 0.60f) == Edge::None);
    CHECK(s.isPressed());
}

// Non-finite and negative inputs are invalid samples, never readings.
static void nonFiniteAndNegative() {
    VROHandPinchState s;
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, kInf) == Edge::None);   // released + invalid: disarm
    CHECK(!s.isArmed());
    CHECK(s.update(true, 0.9f) == Edge::None);   // needs a release first
    CHECK(s.update(true, 0.1f) == Edge::None);
    CHECK(s.update(true, -kInf) == Edge::None);
    CHECK(!s.isArmed());
    CHECK(s.update(true, 0.1f) == Edge::None);
    CHECK(s.update(true, kNaN) == Edge::None);
    CHECK(!s.isArmed());

    // Negative strength is finite: it reads as "released" for the strength
    // metric. Negative distance is rejected as invalid.
    VROHandPinchState d;
    CHECK(d.updateDistance(true, 0.030f) == Edge::None);
    CHECK(d.isArmed());
    CHECK(d.updateDistance(true, -0.001f) == Edge::None);
    CHECK(!d.isArmed());
    CHECK(d.updateDistance(true, 0.030f) == Edge::None);
    CHECK(d.updateDistance(true, 0.010f) == Edge::Down);
    CHECK(d.updateDistance(true, kNaN) == Edge::None);  // first invalid frame: grace
    CHECK(d.isPressed());
    CHECK(d.updateDistance(true, 0.010f) == Edge::None);  // recovered
    CHECK(d.invalidSampleCount() == 0);
}

// Fix 3: grace window. kGrace-1 invalid frames keep the press; the kGrace-th
// cancels; the cancel is emitted exactly once.
static void graceWindow() {
    CHECK(kGrace == 3);
    VROHandPinchState s;
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, 0.9f) == Edge::Down);

    // Two invalid frames: still pressed.
    CHECK(s.update(false, 0.0f) == Edge::None);
    CHECK(s.isPressed());
    CHECK(s.invalidSampleCount() == 1);
    CHECK(s.invalidSample() == Edge::None);
    CHECK(s.isPressed());
    CHECK(s.invalidSampleCount() == 2);
    // A valid frame resets the counter.
    CHECK(s.update(true, 0.9f) == Edge::None);
    CHECK(s.invalidSampleCount() == 0);

    // Three invalid frames: cancel on the third, once.
    CHECK(s.update(false, 0.0f) == Edge::None);
    CHECK(s.update(true, kNaN) == Edge::None);
    CHECK(s.isPressed());
    CHECK(s.invalidSample() == Edge::Cancel);
    CHECK(!s.isPressed());
    CHECK(!s.isArmed());
    // Tracking stays lost: no further edges.
    CHECK(s.invalidSample() == Edge::None);
    CHECK(s.update(false, 0.0f) == Edge::None);
    CHECK(s.invalidSample() == Edge::None);

    // Press from the other metric gets the same window.
    VROHandPinchState d;
    CHECK(d.updateDistance(true, 0.030f) == Edge::None);
    CHECK(d.updateDistance(true, 0.010f) == Edge::Down);
    for (int i = 1; i < kGrace; ++i) {
        CHECK(d.invalidSample() == Edge::None);
        CHECK(d.isPressed());
    }
    CHECK(d.invalidSample() == Edge::Cancel);
    CHECK(d.invalidSample() == Edge::None);
}

// Fix 3: system gesture / menu use cancel(), which has no grace.
static void systemGestureImmediateCancel() {
    VROHandPinchState s;
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, 0.9f) == Edge::Down);
    CHECK(s.cancel() == Edge::Cancel);
    CHECK(!s.isPressed());
    CHECK(s.invalidSampleCount() == 0);
    CHECK(s.latchedMetric() == Metric::None);
    // Cancel while released is a no-op edge.
    CHECK(s.cancel() == Edge::None);
    // Also mid-grace: cancel fires at once, not after the window.
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, 0.9f) == Edge::Down);
    CHECK(s.invalidSample() == Edge::None);
    CHECK(s.cancel() == Edge::Cancel);
    CHECK(s.invalidSample() == Edge::None);
}

// Fix 1: the metric that opened the press is latched. A sample from the other
// metric is an invalid sample, not a reading, even if it would read as
// "released" or "pressed" on its own scale.
static void metricLatchAcrossPress() {
    // Strength press, then distance samples arrive.
    VROHandPinchState s;
    CHECK(s.update(true, 0.0f) == Edge::None);
    CHECK(s.update(true, 0.9f) == Edge::Down);
    CHECK(s.latchedMetric() == Metric::Strength);
    CHECK(s.updateDistance(true, 0.050f) == Edge::None);  // would be "released": ignored
    CHECK(s.isPressed());
    CHECK(s.latchedMetric() == Metric::Strength);
    CHECK(s.invalidSampleCount() == 1);
    CHECK(s.update(true, 0.9f) == Edge::None);            // strength returns: hold
    CHECK(s.invalidSampleCount() == 0);
    CHECK(s.update(true, 0.3f) == Edge::Up);              // only strength can release
    CHECK(s.latchedMetric() == Metric::None);

    // Distance press, then strength samples arrive for the whole window.
    VROHandPinchState d;
    CHECK(d.updateDistance(true, 0.030f) == Edge::None);
    CHECK(d.updateDistance(true, 0.010f) == Edge::Down);
    CHECK(d.latchedMetric() == Metric::Distance);
    CHECK(d.update(true, 0.0f) == Edge::None);   // "released" on strength: dropout 1
    CHECK(d.update(true, 1.0f) == Edge::None);   // "pressed" on strength: dropout 2
    CHECK(d.isPressed());
    CHECK(d.update(true, 0.0f) == Edge::Cancel); // dropout 3: cancel, once
    CHECK(!d.isPressed());
    CHECK(d.update(true, 0.0f) == Edge::None);

    // While released the metric may change, but the new metric must observe
    // a released sample before it can arm.
    VROHandPinchState m;
    CHECK(m.update(true, 0.0f) == Edge::None);
    CHECK(m.isArmed());
    CHECK(m.updateDistance(true, 0.010f) == Edge::None);  // metric changed: no Down
    CHECK(!m.isArmed());
    CHECK(m.updateDistance(true, 0.030f) == Edge::None);  // released on distance
    CHECK(m.updateDistance(true, 0.010f) == Edge::Down);
    CHECK(m.latchedMetric() == Metric::Distance);
}

// Two hands are independent instances.
static void independentHands() {
    VROHandPinchState left, right;
    CHECK(left.update(true, 0.0f) == Edge::None);
    CHECK(right.update(true, 0.0f) == Edge::None);
    CHECK(left.update(true, 0.8f) == Edge::Down);
    CHECK(right.update(true, 0.8f) == Edge::Down);
    CHECK(left.cancel() == Edge::Cancel);
    CHECK(right.isPressed());
    CHECK(right.update(true, 0.55f) == Edge::Up);
}

int main() {
    directionValidity();
    firstSampleAlreadyPinched();
    rearmAfterCancel();
    hysteresis();
    thresholdEdges();
    nonFiniteAndNegative();
    graceWindow();
    systemGestureImmediateCancel();
    metricLatchAcrossPress();
    independentHands();
    if (g_failures != 0) {
        std::fprintf(stderr, "VROHandPinchStateTest: %d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("VROHandPinchStateTest: all checks passed\n");
    return EXIT_SUCCESS;
}
