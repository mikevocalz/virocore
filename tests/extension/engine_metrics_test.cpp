#include "VROEngineMetrics.h"

#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

void measuredWork() {
    VRO_ENGINE_METRIC_SCOPE_TIMER(timer);
    VRO_ENGINE_METRIC_ALLOCATION(128);
    VRO_ENGINE_METRIC_COPY(64);
    VRO_ENGINE_METRIC_FREE(128);
}

} // namespace

int main() {
    bool ok = true;

    viro_engine_metrics_reset();
    measuredWork();

    VROEngineMetricSnapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    ok &= expect(viro_engine_metrics_snapshot(&snapshot) == 0,
                 "snapshot must succeed");
    ok &= expect(snapshot.metric_count == VRO_ENGINE_METRIC_COUNT,
                 "metric count must be stable");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_ALLOCATION_COUNT] == 1,
                 "allocation count");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_ALLOCATED_BYTES] == 128,
                 "allocated bytes");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_FREE_COUNT] == 1,
                 "free count");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_FREED_BYTES] == 128,
                 "freed bytes");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_COPY_COUNT] == 1,
                 "copy count");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_COPIED_BYTES] == 64,
                 "copied bytes");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_CALL_COUNT] == 1,
                 "scope timer call count");
    ok &= expect(snapshot.values[VRO_ENGINE_METRIC_ELAPSED_NS] > 0,
                 "elapsed nanoseconds");

    VROEngineMetricSnapshot tooSmall{};
    tooSmall.struct_size = sizeof(uint32_t);
    ok &= expect(viro_engine_metrics_snapshot(&tooSmall) == -1,
                 "undersized snapshots must fail");

    if (!ok) return EXIT_FAILURE;
    std::cout << "Viro engine metrics substrate passed\n";
    return EXIT_SUCCESS;
}
