#include "VROEngineSubsystemMetrics.h"

#include <cassert>
#include <cstdint>
#include <iostream>

static void measuredPhysicsWork() {
    VRO_ENGINE_SUBSYSTEM_SCOPE_TIMER(
        VRO_ENGINE_SUBSYSTEM_PHYSICS, timer);
    VRO_ENGINE_SUBSYSTEM_WORK(VRO_ENGINE_SUBSYSTEM_PHYSICS, 17);
    VRO_ENGINE_SUBSYSTEM_PAYLOAD(VRO_ENGINE_SUBSYSTEM_PHYSICS, 512);
}

int main() {
    viro_engine_subsystem_metrics_reset();
    measuredPhysicsWork();

    VRO_ENGINE_SUBSYSTEM_CALL(VRO_ENGINE_SUBSYSTEM_PARTICLES);
    VRO_ENGINE_SUBSYSTEM_WORK(VRO_ENGINE_SUBSYSTEM_PARTICLES, 3);

    VROEngineSubsystemMetricSnapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    assert(viro_engine_subsystem_metrics_snapshot(&snapshot) == 1);
    assert(snapshot.subsystem_count == VRO_ENGINE_SUBSYSTEM_SLOT_COUNT);

    const auto *physics = snapshot.values[VRO_ENGINE_SUBSYSTEM_PHYSICS];
    assert(physics[VRO_ENGINE_SUBSYSTEM_CALL_COUNT] == 1);
    assert(physics[VRO_ENGINE_SUBSYSTEM_ELAPSED_NS] > 0);
    assert(physics[VRO_ENGINE_SUBSYSTEM_WORK_ITEMS] == 17);
    assert(physics[VRO_ENGINE_SUBSYSTEM_PAYLOAD_BYTES] == 512);

    const auto *particles = snapshot.values[VRO_ENGINE_SUBSYSTEM_PARTICLES];
    assert(particles[VRO_ENGINE_SUBSYSTEM_CALL_COUNT] == 1);
    assert(particles[VRO_ENGINE_SUBSYSTEM_WORK_ITEMS] == 3);

    std::cout << "Engine subsystem metrics: PASS\n";
    return 0;
}
