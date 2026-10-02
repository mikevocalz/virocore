#include "VROEngineSubsystemMetrics.h"

_Static_assert(sizeof(VROEngineSubsystemMetricSnapshot) ==
                   VRO_ENGINE_SUBSYSTEM_METRIC_SNAPSHOT_V0_1_SIZE,
               "subsystem metrics C ABI changed");

int viro_subsystem_metrics_c_probe(void) {
    VROEngineSubsystemMetricSnapshot snapshot = {0};
    snapshot.struct_size = sizeof(snapshot);
    return (int)snapshot.struct_size;
}
