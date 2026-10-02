#include "VROEngineSpatialBackend.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}

bool expect(bool value, const char *label) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        return false;
    }
    return true;
}

bool runBackend(
    VROEngineBackendPreference preference,
    VROEngineBackendKind expectedBackend) {
    viro_engine_backend_preferences_reset();
    if (viro_engine_backend_preference_set(
            VRO_ENGINE_DOMAIN_SPATIAL,
            preference) != VRO_ENGINE_STATUS_OK) {
        return false;
    }

    bool ok = expect(
        viro_engine_spatial_backend_resolve() == expectedBackend,
        "spatial backend");

    VROEngineRigidTransform identity{};
    identity.struct_size = sizeof(identity);
    ok &= expect(
        viro_engine_transform_identity_selected(&identity) ==
            VRO_ENGINE_STATUS_OK,
        "identity");
    ok &= expect(
        near(identity.rotation.w, 1.0f),
        "identity quaternion");

    VROEngineRigidTransform parent{};
    parent.struct_size = sizeof(parent);
    parent.translation = {1.0f, 2.0f, 3.0f};
    parent.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    VROEngineRigidTransform child{};
    child.struct_size = sizeof(child);
    child.translation = {4.0f, 5.0f, 6.0f};
    child.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    VROEngineRigidTransform composed{};
    composed.struct_size = sizeof(composed);
    ok &= expect(
        viro_engine_transform_compose_selected(
            &parent, &child, &composed) == VRO_ENGINE_STATUS_OK,
        "compose");
    ok &= expect(
        near(composed.translation.x, 5.0f) &&
        near(composed.translation.y, 7.0f) &&
        near(composed.translation.z, 9.0f),
        "compose translation");

    VROEngineRigidTransform inverse{};
    inverse.struct_size = sizeof(inverse);
    ok &= expect(
        viro_engine_transform_invert_selected(
            &parent, &inverse) == VRO_ENGINE_STATUS_OK,
        "invert");

    VROEngineVec3 point{2.0f, 3.0f, 4.0f};
    VROEngineVec3 world{};
    ok &= expect(
        viro_engine_transform_point_selected(
            &parent, &point, &world) == VRO_ENGINE_STATUS_OK,
        "point transform");
    ok &= expect(
        near(world.x, 3.0f) &&
        near(world.y, 5.0f) &&
        near(world.z, 7.0f),
        "point value");

    VROEngineSharedFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.tracking_state = VRO_ENGINE_TRACKING_NORMAL;
    frame.local_from_shared = identity;
    frame.confidence = 1.0f;
    ok &= expect(
        viro_engine_shared_frame_validate_selected(&frame) ==
            VRO_ENGINE_STATUS_OK,
        "shared frame");

    frame.confidence = std::nanf("");
    ok &= expect(
        viro_engine_shared_frame_validate_selected(&frame) ==
            VRO_ENGINE_STATUS_INVALID_ARGUMENT,
        "nan confidence rejected");

    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= expect(
        (viro_engine_spatial_backend_availability() &
         VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) != 0,
        "Eskiu spatial compiled");
    ok &= expect(
        runBackend(VRO_ENGINE_BACKEND_PREFERENCE_AUTO,
                   VRO_ENGINE_BACKEND_CPP),
        "AUTO remains C++");
    ok &= expect(
        runBackend(VRO_ENGINE_BACKEND_PREFERENCE_CPP,
                   VRO_ENGINE_BACKEND_CPP),
        "explicit C++");
    ok &= expect(
        runBackend(VRO_ENGINE_BACKEND_PREFERENCE_ESKIU,
                   VRO_ENGINE_BACKEND_ESKIU),
        "explicit Eskiu");
    viro_engine_backend_preferences_reset();

    if (!ok) return EXIT_FAILURE;
    std::cout << "Eskiu spatial runtime backend passed\n";
    return EXIT_SUCCESS;
}
