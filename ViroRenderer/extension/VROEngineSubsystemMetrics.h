//
// VROEngineSubsystemMetrics.h
//
// Named, opt-in subsystem measurements. Kept separate from the frozen generic
// VROEngineMetricSnapshot ABI so profiling domains can coexist without merging
// all call/timing data into one counter.
//

#ifndef VRO_ENGINE_SUBSYSTEM_METRICS_H
#define VRO_ENGINE_SUBSYSTEM_METRICS_H

#include <stdint.h>
#include "VROEngineMetrics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_SUBSYSTEM_METRICS_ABI_MAJOR 0u
#define VRO_ENGINE_SUBSYSTEM_METRICS_ABI_MINOR 1u
#define VRO_ENGINE_SUBSYSTEM_SLOT_COUNT 8u
#define VRO_ENGINE_SUBSYSTEM_METRIC_COUNT 5u

typedef enum VROEngineSubsystemId {
    VRO_ENGINE_SUBSYSTEM_PHYSICS = 0,
    VRO_ENGINE_SUBSYSTEM_PARTICLES = 1,
    VRO_ENGINE_SUBSYSTEM_POST_PROCESS = 2,
    VRO_ENGINE_SUBSYSTEM_WORLD_MESH = 3,
    VRO_ENGINE_SUBSYSTEM_MEDIA = 4,
    VRO_ENGINE_SUBSYSTEM_ASSET_LOADING = 5,
    VRO_ENGINE_SUBSYSTEM_RESERVED_6 = 6,
    VRO_ENGINE_SUBSYSTEM_RESERVED_7 = 7
} VROEngineSubsystemId;

typedef enum VROEngineSubsystemMetricId {
    VRO_ENGINE_SUBSYSTEM_CALL_COUNT = 0,
    VRO_ENGINE_SUBSYSTEM_ELAPSED_NS = 1,
    VRO_ENGINE_SUBSYSTEM_WORK_ITEMS = 2,
    VRO_ENGINE_SUBSYSTEM_PAYLOAD_BYTES = 3,
    VRO_ENGINE_SUBSYSTEM_ALLOCATION_COUNT = 4
} VROEngineSubsystemMetricId;

typedef struct VROEngineSubsystemMetricSnapshot {
    uint32_t struct_size;
    uint32_t subsystem_count;
    uint64_t values[VRO_ENGINE_SUBSYSTEM_SLOT_COUNT]
                   [VRO_ENGINE_SUBSYSTEM_METRIC_COUNT];
} VROEngineSubsystemMetricSnapshot;

#define VRO_ENGINE_SUBSYSTEM_METRIC_SNAPSHOT_V0_1_SIZE 328u

void viro_engine_subsystem_metrics_reset(void);
void viro_engine_subsystem_metrics_add(
    VROEngineSubsystemId subsystem,
    VROEngineSubsystemMetricId metric,
    uint64_t value);
int32_t viro_engine_subsystem_metrics_snapshot(
    VROEngineSubsystemMetricSnapshot *out_snapshot);

#ifdef __cplusplus
} // extern "C"

static_assert(
    sizeof(VROEngineSubsystemMetricSnapshot) ==
        VRO_ENGINE_SUBSYSTEM_METRIC_SNAPSHOT_V0_1_SIZE,
    "Subsystem metric snapshot ABI v0.1 must stay 328 bytes");

#if VRO_ENGINE_METRICS_ENABLED
class VROEngineSubsystemMetricTimer {
public:
    explicit VROEngineSubsystemMetricTimer(VROEngineSubsystemId subsystem);
    ~VROEngineSubsystemMetricTimer();

    VROEngineSubsystemMetricTimer(const VROEngineSubsystemMetricTimer &) = delete;
    VROEngineSubsystemMetricTimer &operator=(
        const VROEngineSubsystemMetricTimer &) = delete;

private:
    VROEngineSubsystemId _subsystem;
    uint64_t _startNs;
};
#endif
#endif

#if VRO_ENGINE_METRICS_ENABLED
#define VRO_ENGINE_SUBSYSTEM_METRIC_ADD(subsystem, metric, value)     viro_engine_subsystem_metrics_add((subsystem), (metric), (uint64_t)(value))
#define VRO_ENGINE_SUBSYSTEM_CALL(subsystem)     viro_engine_subsystem_metrics_add(         (subsystem), VRO_ENGINE_SUBSYSTEM_CALL_COUNT, 1)
#define VRO_ENGINE_SUBSYSTEM_WORK(subsystem, count)     viro_engine_subsystem_metrics_add(         (subsystem), VRO_ENGINE_SUBSYSTEM_WORK_ITEMS, (uint64_t)(count))
#define VRO_ENGINE_SUBSYSTEM_PAYLOAD(subsystem, bytes)     viro_engine_subsystem_metrics_add(         (subsystem), VRO_ENGINE_SUBSYSTEM_PAYLOAD_BYTES, (uint64_t)(bytes))
#define VRO_ENGINE_SUBSYSTEM_ALLOCATION(subsystem)     viro_engine_subsystem_metrics_add(         (subsystem), VRO_ENGINE_SUBSYSTEM_ALLOCATION_COUNT, 1)
#define VRO_ENGINE_SUBSYSTEM_SCOPE_TIMER(subsystem, name)     VROEngineSubsystemMetricTimer name(subsystem)
#else
#define VRO_ENGINE_SUBSYSTEM_METRIC_ADD(subsystem, metric, value) ((void)0)
#define VRO_ENGINE_SUBSYSTEM_CALL(subsystem) ((void)0)
#define VRO_ENGINE_SUBSYSTEM_WORK(subsystem, count) ((void)0)
#define VRO_ENGINE_SUBSYSTEM_PAYLOAD(subsystem, bytes) ((void)0)
#define VRO_ENGINE_SUBSYSTEM_ALLOCATION(subsystem) ((void)0)
#define VRO_ENGINE_SUBSYSTEM_SCOPE_TIMER(subsystem, name) ((void)0)
#endif

#endif // VRO_ENGINE_SUBSYSTEM_METRICS_H
