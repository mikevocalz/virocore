#include "VROEngineInputShadow.h"

#include <cstdlib>
#include <iostream>

// Keep this focused adapter test independent of the full renderer math link graph.
// The adapter reads only the public x/y/z fields, so these constructor definitions
// are sufficient for the fixture and avoid pulling VROMatrix/VROMath into the test.
VROVector3f::VROVector3f() noexcept : x(0.0f), y(0.0f), z(0.0f) {}
VROVector3f::VROVector3f(float xValue, float yValue, float zValue)
    : x(xValue), y(yValue), z(zValue) {}

namespace {

bool expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool same(float a, float b) {
    const float d = a - b;
    return d < 0.0001f && d > -0.0001f;
}

} // namespace

int main() {
    bool ok = true;
    VROEngineInputShadow shadow(4);
    ok &= expect(shadow.isReady(), "shadow ring ready");

    VROVector3f controllerPos(1.0f, 2.0f, 3.0f);
    VROQuaternion controllerRot(0.1f, 0.2f, 0.3f, 0.9f);
    const float axes[4] = {0.25f, -0.5f, 0.75f, 1.0f};

    ok &= expect(
        shadow.mirrorController(
            11, 100, 200, controllerPos, controllerRot,
            5, axes, 0.6f, 0.4f) == VRO_ENGINE_INPUT_OK,
        "mirror controller");

    VROVector3f stylusPos(4.0f, 5.0f, 6.0f);
    VROQuaternion stylusRot(0.0f, 0.0f, 0.0f, 1.0f);
    ok &= expect(
        shadow.mirrorStylus(
            12, 101, 201, stylusPos, stylusRot,
            0.8f, 0.1f, -0.2f, 3) == VRO_ENGINE_INPUT_OK,
        "mirror stylus");

    VROVector3f gazeOrigin(7.0f, 8.0f, 9.0f);
    VROVector3f gazeDirection(0.0f, 0.0f, -1.0f);
    ok &= expect(
        shadow.mirrorGaze(
            13, 102, 202, gazeOrigin, gazeDirection, 0.95f) ==
            VRO_ENGINE_INPUT_OK,
        "mirror gaze");

    VROVector3f jointPos(10.0f, 11.0f, 12.0f);
    VROQuaternion jointRot(0.2f, 0.3f, 0.4f, 0.8f);
    ok &= expect(
        shadow.mirrorHandJoint(
            14, 103, 203, 9, jointPos, jointRot, 0.012f, 0.9f) ==
            VRO_ENGINE_INPUT_OK,
        "mirror hand joint");

    ok &= expect(shadow.size() == 4, "four samples queued");

    VROEngineInputSample out{};
    out.struct_size = sizeof(out);

    ok &= expect(shadow.pop(&out) == VRO_ENGINE_INPUT_OK, "pop controller");
    ok &= expect(out.sequence == 1, "controller sequence");
    ok &= expect(out.kind == VRO_ENGINE_INPUT_CONTROLLER, "controller kind");
    ok &= expect(out.source_id == 11, "controller source");
    ok &= expect(same(out.payload.controller.position.x, 1.0f), "controller x");
    ok &= expect(same(out.payload.controller.orientation.w, 0.9f), "controller quat");
    ok &= expect(out.payload.controller.buttons == 5, "controller buttons");
    ok &= expect(same(out.payload.controller.axes[1], -0.5f), "controller axis");
    ok &= expect(same(out.payload.controller.trigger, 0.6f), "controller trigger");

    out.struct_size = sizeof(out);
    ok &= expect(shadow.pop(&out) == VRO_ENGINE_INPUT_OK, "pop stylus");
    ok &= expect(out.sequence == 2, "stylus sequence");
    ok &= expect(out.kind == VRO_ENGINE_INPUT_STYLUS, "stylus kind");
    ok &= expect(same(out.payload.stylus.pressure, 0.8f), "stylus pressure");

    out.struct_size = sizeof(out);
    ok &= expect(shadow.pop(&out) == VRO_ENGINE_INPUT_OK, "pop gaze");
    ok &= expect(out.sequence == 3, "gaze sequence");
    ok &= expect(out.kind == VRO_ENGINE_INPUT_GAZE, "gaze kind");
    ok &= expect(same(out.payload.gaze.direction.z, -1.0f), "gaze direction");

    out.struct_size = sizeof(out);
    ok &= expect(shadow.pop(&out) == VRO_ENGINE_INPUT_OK, "pop hand");
    ok &= expect(out.sequence == 4, "hand sequence");
    ok &= expect(out.kind == VRO_ENGINE_INPUT_HAND_JOINT, "hand kind");
    ok &= expect(out.payload.hand_joint.joint_index == 9, "joint index");
    ok &= expect(same(out.payload.hand_joint.confidence, 0.9f), "joint confidence");

    // Saturation remains visible rather than overwriting an existing transition.
    for (int i = 0; i < 4; ++i) {
        ok &= expect(
            shadow.mirrorController(
                1, 300 + i, 400 + i, controllerPos, controllerRot) ==
                VRO_ENGINE_INPUT_OK,
            "fill ring");
    }
    ok &= expect(
        shadow.mirrorController(
            1, 999, 999, controllerPos, controllerRot) ==
            VRO_ENGINE_INPUT_FULL,
        "shadow reports saturation");
    ok &= expect(shadow.droppedCount() == 1, "shadow dropped count");

    if (!ok) return EXIT_FAILURE;
    std::cout << "Viro C++ input shadow adapter passed\n";
    return EXIT_SUCCESS;
}
