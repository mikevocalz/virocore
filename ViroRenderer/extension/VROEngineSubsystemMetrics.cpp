//
// VROEngineSubsystemMetrics.cpp
//

#include "VROEngineSubsystemMetrics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>

namespace {

std::atomic<uint64_t>
    gValues[VRO_ENGINE_SUBSYSTEM_SLOT_COUNT][VRO_ENGINE_SUBSYSTEM_METRIC_COUNT]{};

bool validSubsystem(VROEngineSubsystemId id) {
    const auto value = static_cast<uint32_t>(id);
    return value < VRO_ENGINE_SUBSYSTEM_SLOT_COUNT;
}

bool validMetric(VROEngineSubsystemMetricId id) {
    const auto value = static_cast<uint32_t>(id);
    return value < VRO_ENGINE_SUBSYSTEM_METRIC_COUNT;
}

uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

extern "C" void viro_engine_subsystem_metrics_reset(void) {
#if VRO_ENGINE_METRICS_ENABLED
    for (uint32_t subsystem = 0;
         subsystem < VRO_ENGINE_SUBSYSTEM_SLOT_COUNT;
         ++subsystem) {
        for (uint32_t metric = 0;
             metric < VRO_ENGINE_SUBSYSTEM_METRIC_COUNT;
             ++metric) {
            gValues[subsystem][metric].store(0, std::memory_order_relaxed);
        }
    }
#endif
}

extern "C" void viro_engine_subsystem_metrics_add(
    VROEngineSubsystemId subsystem,
    VROEngineSubsystemMetricId metric,
    uint64_t value) {
#if VRO_ENGINE_METRICS_ENABLED
    if (!validSubsystem(subsystem) || !validMetric(metric)) {
        return;
    }
    gValues[static_cast<uint32_t>(subsystem)]
           [static_cast<uint32_t>(metric)]
        .fetch_add(value, std::memory_order_relaxed);
#else
    (void)subsystem;
    (void)metric;
    (void)value;
#endif
}

extern "C" int32_t viro_engine_subsystem_metrics_snapshot(
    VROEngineSubsystemMetricSnapshot *out_snapshot) {
    if (out_snapshot == nullptr ||
        out_snapshot->struct_size <
            VRO_ENGINE_SUBSYSTEM_METRIC_SNAPSHOT_V0_1_SIZE) {
        return 0;
    }

    VROEngineSubsystemMetricSnapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.subsystem_count = VRO_ENGINE_SUBSYSTEM_SLOT_COUNT;
#if VRO_ENGINE_METRICS_ENABLED
    for (uint32_t subsystem = 0;
         subsystem < VRO_ENGINE_SUBSYSTEM_SLOT_COUNT;
         ++subsystem) {
        for (uint32_t metric = 0;
             metric < VRO_ENGINE_SUBSYSTEM_METRIC_COUNT;
             ++metric) {
            snapshot.values[subsystem][metric] =
                gValues[subsystem][metric].load(std::memory_order_relaxed);
        }
    }
#endif

    const size_t writeSize =
        std::min<size_t>(out_snapshot->struct_size, sizeof(snapshot));
    std::memcpy(out_snapshot, &snapshot, writeSize);
    return 1;
}

#if VRO_ENGINE_METRICS_ENABLED
VROEngineSubsystemMetricTimer::VROEngineSubsystemMetricTimer(
    VROEngineSubsystemId subsystem)
    : _subsystem(subsystem), _startNs(nowNs()) {
    viro_engine_subsystem_metrics_add(
        subsystem, VRO_ENGINE_SUBSYSTEM_CALL_COUNT, 1);
}

VROEngineSubsystemMetricTimer::~VROEngineSubsystemMetricTimer() {
    viro_engine_subsystem_metrics_add(
        _subsystem,
        VRO_ENGINE_SUBSYSTEM_ELAPSED_NS,
        nowNs() - _startNs);
}
#endif
