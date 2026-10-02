//
// VROEngineMetrics.h
//
// Opt-in measurement substrate for C++ / Eskiu backend comparisons.
// Disabled by default: production call sites compile to no-ops unless
// VRO_ENGINE_METRICS_ENABLED=1 is defined for the target.
//

#ifndef VRO_ENGINE_METRICS_H
#define VRO_ENGINE_METRICS_H

#include <stdint.h>

#ifndef VRO_ENGINE_METRICS_ENABLED
#define VRO_ENGINE_METRICS_ENABLED 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VROEngineMetricId {
    VRO_ENGINE_METRIC_ALLOCATION_COUNT = 0,
    VRO_ENGINE_METRIC_ALLOCATED_BYTES = 1,
    VRO_ENGINE_METRIC_FREE_COUNT = 2,
    VRO_ENGINE_METRIC_FREED_BYTES = 3,
    VRO_ENGINE_METRIC_COPY_COUNT = 4,
    VRO_ENGINE_METRIC_COPIED_BYTES = 5,
    VRO_ENGINE_METRIC_CALL_COUNT = 6,
    VRO_ENGINE_METRIC_ELAPSED_NS = 7,
    VRO_ENGINE_METRIC_COUNT = 8
} VROEngineMetricId;

typedef struct VROEngineMetricSnapshot {
    uint32_t struct_size;
    uint32_t metric_count;
    uint64_t values[VRO_ENGINE_METRIC_COUNT];
} VROEngineMetricSnapshot;

#define VRO_ENGINE_METRIC_SNAPSHOT_V0_1_SIZE 72u

void viro_engine_metrics_reset(void);
void viro_engine_metrics_add(VROEngineMetricId metric, uint64_t value);
int32_t viro_engine_metrics_snapshot(VROEngineMetricSnapshot *out_snapshot);

#ifdef __cplusplus
static_assert(sizeof(VROEngineMetricSnapshot) == 72, "Metric snapshot ABI v0.1 must stay 72 bytes");
} // extern "C"

#if VRO_ENGINE_METRICS_ENABLED
class VROEngineMetricTimer {
public:
    VROEngineMetricTimer();
    ~VROEngineMetricTimer();

    VROEngineMetricTimer(const VROEngineMetricTimer &) = delete;
    VROEngineMetricTimer &operator=(const VROEngineMetricTimer &) = delete;

private:
    uint64_t _startNs;
};
#endif
#endif

#if VRO_ENGINE_METRICS_ENABLED
#define VRO_ENGINE_METRIC_ADD(metric, value) \
    viro_engine_metrics_add((metric), (uint64_t)(value))
#define VRO_ENGINE_METRIC_ALLOCATION(bytes) do { \
    viro_engine_metrics_add(VRO_ENGINE_METRIC_ALLOCATION_COUNT, 1); \
    viro_engine_metrics_add(VRO_ENGINE_METRIC_ALLOCATED_BYTES, (uint64_t)(bytes)); \
} while (0)
#define VRO_ENGINE_METRIC_FREE(bytes) do { \
    viro_engine_metrics_add(VRO_ENGINE_METRIC_FREE_COUNT, 1); \
    viro_engine_metrics_add(VRO_ENGINE_METRIC_FREED_BYTES, (uint64_t)(bytes)); \
} while (0)
#define VRO_ENGINE_METRIC_COPY(bytes) do { \
    viro_engine_metrics_add(VRO_ENGINE_METRIC_COPY_COUNT, 1); \
    viro_engine_metrics_add(VRO_ENGINE_METRIC_COPIED_BYTES, (uint64_t)(bytes)); \
} while (0)
#define VRO_ENGINE_METRIC_CALL() \
    viro_engine_metrics_add(VRO_ENGINE_METRIC_CALL_COUNT, 1)
#define VRO_ENGINE_METRIC_SCOPE_TIMER(name) VROEngineMetricTimer name
#else
#define VRO_ENGINE_METRIC_ADD(metric, value) ((void)0)
#define VRO_ENGINE_METRIC_ALLOCATION(bytes) ((void)0)
#define VRO_ENGINE_METRIC_FREE(bytes) ((void)0)
#define VRO_ENGINE_METRIC_COPY(bytes) ((void)0)
#define VRO_ENGINE_METRIC_CALL() ((void)0)
#define VRO_ENGINE_METRIC_SCOPE_TIMER(name) ((void)0)
#endif

#endif // VRO_ENGINE_METRICS_H
