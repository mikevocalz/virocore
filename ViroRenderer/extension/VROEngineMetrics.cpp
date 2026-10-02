//
// VROEngineMetrics.cpp
//

#include "VROEngineMetrics.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#if VRO_ENGINE_METRICS_ENABLED
#include <chrono>
#endif

namespace {

#if VRO_ENGINE_METRICS_ENABLED
std::atomic<uint64_t> gMetricValues[VRO_ENGINE_METRIC_COUNT];

uint64_t nowNs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}
#endif

#if VRO_ENGINE_METRICS_ENABLED
bool validMetric(VROEngineMetricId metric) {
    const int value = static_cast<int>(metric);
    return value >= 0 && value < VRO_ENGINE_METRIC_COUNT;
}
#endif

} // namespace

extern "C" void viro_engine_metrics_reset(void) {
#if VRO_ENGINE_METRICS_ENABLED
    for (int i = 0; i < VRO_ENGINE_METRIC_COUNT; ++i) {
        gMetricValues[i].store(0, std::memory_order_relaxed);
    }
#endif
}

extern "C" void viro_engine_metrics_add(VROEngineMetricId metric, uint64_t value) {
#if VRO_ENGINE_METRICS_ENABLED
    if (!validMetric(metric)) {
        return;
    }
    gMetricValues[static_cast<int>(metric)].fetch_add(value, std::memory_order_relaxed);
#else
    (void)metric;
    (void)value;
#endif
}

extern "C" int32_t viro_engine_metrics_snapshot(VROEngineMetricSnapshot *out_snapshot) {
    if (out_snapshot == nullptr ||
        out_snapshot->struct_size < VRO_ENGINE_METRIC_SNAPSHOT_V0_1_SIZE) {
        return -1;
    }

    VROEngineMetricSnapshot snapshot{};
    snapshot.struct_size = static_cast<uint32_t>(sizeof(snapshot));
    snapshot.metric_count = VRO_ENGINE_METRIC_COUNT;

#if VRO_ENGINE_METRICS_ENABLED
    for (int i = 0; i < VRO_ENGINE_METRIC_COUNT; ++i) {
        snapshot.values[i] = gMetricValues[i].load(std::memory_order_relaxed);
    }
#endif

    const size_t writeSize = std::min<size_t>(
        out_snapshot->struct_size, sizeof(VROEngineMetricSnapshot));
    std::memcpy(out_snapshot, &snapshot, writeSize);
    return 0;
}

#if VRO_ENGINE_METRICS_ENABLED
VROEngineMetricTimer::VROEngineMetricTimer() : _startNs(nowNs()) {
    viro_engine_metrics_add(VRO_ENGINE_METRIC_CALL_COUNT, 1);
}

VROEngineMetricTimer::~VROEngineMetricTimer() {
    const uint64_t endNs = nowNs();
    viro_engine_metrics_add(
        VRO_ENGINE_METRIC_ELAPSED_NS,
        endNs >= _startNs ? endNs - _startNs : 0);
}
#endif
