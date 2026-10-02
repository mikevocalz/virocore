#include "VROEngineSpatialABI.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
bool close(float a, float b) { return std::fabs(a-b) < 0.0001f; }
bool expect(bool c, const char *m) {
    if (!c) std::cerr << "FAIL: " << m << "\n";
    return c;
}
VROEngineRigidTransform transform(
    float x,float y,float z,
    float qx,float qy,float qz,float qw) {
    VROEngineRigidTransform t{};
    t.struct_size = sizeof(t);
    t.translation = {x,y,z};
    t.rotation = {qx,qy,qz,qw};
    return t;
}
}

int main() {
    bool ok = true;

    VROEngineRigidTransform id{};
    id.struct_size = sizeof(id);
    ok &= expect(viro_engine_transform_identity(&id) == VRO_ENGINE_STATUS_OK,
                 "identity");
    ok &= expect(close(id.rotation.w, 1.0f), "identity rotation");

    const float s = std::sqrt(0.5f);
    auto worldFromParent = transform(10,0,0, 0,0,s,s); // +90deg around Z
    auto parentFromChild = transform(2,0,0, 0,0,0,1);

    VROEngineRigidTransform worldFromChild{};
    worldFromChild.struct_size = sizeof(worldFromChild);
    ok &= expect(viro_engine_transform_compose(
        &worldFromParent,&parentFromChild,&worldFromChild) == VRO_ENGINE_STATUS_OK,
        "compose");
    ok &= expect(close(worldFromChild.translation.x,10.0f), "composed x");
    ok &= expect(close(worldFromChild.translation.y,2.0f), "composed y");

    VROEngineRigidTransform childFromWorld{};
    childFromWorld.struct_size = sizeof(childFromWorld);
    ok &= expect(viro_engine_transform_invert(
        &worldFromChild,&childFromWorld) == VRO_ENGINE_STATUS_OK,
        "invert");

    VROEngineVec3 p{3,4,5};
    VROEngineVec3 world{};
    VROEngineVec3 roundtrip{};
    ok &= expect(viro_engine_transform_point(&worldFromChild,&p,&world) ==
                     VRO_ENGINE_STATUS_OK, "transform point");
    ok &= expect(viro_engine_transform_point(&childFromWorld,&world,&roundtrip) ==
                     VRO_ENGINE_STATUS_OK, "inverse point");
    ok &= expect(close(roundtrip.x,p.x) && close(roundtrip.y,p.y) && close(roundtrip.z,p.z),
                 "roundtrip");

    VROEngineSharedFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.tracking_state = VRO_ENGINE_TRACKING_NORMAL;
    frame.frame_id = 42;
    frame.revision = 3;
    frame.timestamp_ns = 1000;
    frame.local_from_shared = worldFromChild;
    frame.confidence = 0.9f;
    ok &= expect(viro_engine_shared_frame_validate(&frame) == VRO_ENGINE_STATUS_OK,
                 "valid shared frame");

    frame.local_from_shared.struct_size = 0;
    ok &= expect(viro_engine_shared_frame_validate(&frame) ==
                     VRO_ENGINE_STATUS_INVALID_ARGUMENT,
                 "invalid nested transform");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
