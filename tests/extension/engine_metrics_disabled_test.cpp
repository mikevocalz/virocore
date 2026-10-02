#include "VROEngineMetrics.h"

#include <cstdlib>

int main() {
    // These macros must remain valid and side-effect free in release/default builds.
    VRO_ENGINE_METRIC_ALLOCATION(99);
    VRO_ENGINE_METRIC_COPY(99);
    VRO_ENGINE_METRIC_FREE(99);
    VRO_ENGINE_METRIC_CALL();

    VROEngineMetricSnapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    if (viro_engine_metrics_snapshot(&snapshot) != 0) return EXIT_FAILURE;
    for (uint32_t i = 0; i < snapshot.metric_count; ++i) {
        if (snapshot.values[i] != 0) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
