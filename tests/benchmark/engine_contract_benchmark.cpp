#include "VROEngineABI.h"
#include "VROEngineGeometryABI.h"
#include "VROEngineInputABI.h"
#include "VROEngineMetrics.h"
#include "VROEngineSpatialABI.h"
#include "VROEngineSurfaceABI.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct Result {
    std::string name;
    double p50_ns;
    double p95_ns;
    uint64_t operations;
};

template <typename Fn>
Result runBatched(const char *name, uint64_t operationsPerBatch, Fn fn) {
    constexpr size_t kBatches = 41;
    std::vector<double> perOp;
    perOp.reserve(kBatches);

    for (size_t batch = 0; batch < kBatches; ++batch) {
        const auto start = Clock::now();
        fn(operationsPerBatch);
        const auto end = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count();
        perOp.push_back(static_cast<double>(elapsed) /
                        static_cast<double>(operationsPerBatch));
    }

    std::sort(perOp.begin(), perOp.end());
    const size_t p50 = perOp.size() / 2;
    const size_t p95 = (perOp.size() * 95) / 100;
    return {
        name,
        perOp[p50],
        perOp[std::min(p95, perOp.size() - 1)],
        operationsPerBatch * kBatches
    };
}

uint64_t peakRssBytes() {
#if defined(__unix__) || defined(__APPLE__)
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
#else
    return 0;
#endif
}

void emitString(const char *key, const std::string &value, bool comma = true) {
    std::cout << "  \"" << key << "\": \"" << value << "\"" << (comma ? "," : "") << "\n";
}

} // namespace

int main(int argc, char **argv) {
    std::string backend = "cpp";
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        constexpr const char *prefix = "--backend=";
        if (arg.rfind(prefix, 0) == 0) {
            backend = arg.substr(std::char_traits<char>::length(prefix));
        }
    }
    if (backend != "cpp") {
        std::cerr << "This binary only contains the C++ reference backend. "
                  << "Link a backend fixture before requesting " << backend << ".\n";
        return 2;
    }

    VROEngineAbiInfo abi{};
    abi.struct_size = sizeof(abi);
    assert(viro_engine_query_abi(&abi) == VRO_ENGINE_STATUS_OK);

    std::vector<Result> results;

    VROEngineInputRing *ring = nullptr;
    assert(viro_engine_input_ring_create(64, &ring) == VRO_ENGINE_INPUT_OK);
    VROEngineInputSample sample{};
    sample.struct_size = sizeof(sample);
    sample.kind = VRO_ENGINE_INPUT_STYLUS;
    sample.payload.stylus.orientation.w = 1.0f;
    VROEngineInputSample out{};
    out.struct_size = sizeof(out);

    results.push_back(runBatched("input_push_pop", 10000, [&](uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) {
            sample.sequence = i;
            sample.timestamp_ns = i;
            const auto push = viro_engine_input_ring_push(ring, &sample);
            assert(push == VRO_ENGINE_INPUT_OK);
            out.struct_size = sizeof(out);
            const auto pop = viro_engine_input_ring_pop(ring, &out);
            assert(pop == VRO_ENGINE_INPUT_OK);
            assert(out.sequence == i);
        }
    }));
    viro_engine_input_ring_destroy(ring);

    VROEngineRigidTransform a{};
    a.struct_size = sizeof(a);
    assert(viro_engine_transform_identity(&a) == VRO_ENGINE_STATUS_OK);
    a.translation = {1.0f, 2.0f, 3.0f};
    VROEngineRigidTransform b{};
    b.struct_size = sizeof(b);
    assert(viro_engine_transform_identity(&b) == VRO_ENGINE_STATUS_OK);
    b.translation = {4.0f, 5.0f, 6.0f};
    VROEngineRigidTransform composed{};
    composed.struct_size = sizeof(composed);

    results.push_back(runBatched("spatial_compose", 20000, [&](uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) {
            composed.struct_size = sizeof(composed);
            assert(viro_engine_transform_compose(&a, &b, &composed) ==
                   VRO_ENGINE_STATUS_OK);
        }
    }));

    VROEngineVertexAttribute attribute{
        VRO_ENGINE_VERTEX_POSITION,
        VRO_ENGINE_COMPONENT_FLOAT32,
        3,
        0
    };
    float vertices[] = {0,0,0, 1,0,0, 0,1,0};
    uint32_t indices[] = {0,1,2};
    VROEngineGeometryDesc geometry{};
    geometry.struct_size = sizeof(geometry);
    geometry.topology = VRO_ENGINE_TOPOLOGY_TRIANGLES;
    geometry.vertex_stride_bytes = sizeof(float) * 3;
    geometry.vertex_count = 3;
    geometry.index_type = VRO_ENGINE_INDEX_UINT32;
    geometry.index_count = 3;
    geometry.attribute_count = 1;
    geometry.attributes = &attribute;
    geometry.vertices = {
        reinterpret_cast<const uint8_t *>(vertices), sizeof(vertices)
    };
    geometry.indices = {
        reinterpret_cast<const uint8_t *>(indices), sizeof(indices)
    };
    geometry.geometry_id = 1;
    geometry.version = 1;

    results.push_back(runBatched("geometry_validate", 50000, [&](uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) {
            assert(viro_engine_geometry_validate(&geometry) ==
                   VRO_ENGINE_STATUS_OK);
        }
    }));

    VROEngineSurfaceDesc surface{};
    surface.struct_size = sizeof(surface);
    surface.width = 2048;
    surface.height = 1024;
    surface.pixel_format = VRO_ENGINE_PIXEL_RGBA8_UNORM;
    surface.origin = VRO_ENGINE_SURFACE_ORIGIN_TOP_LEFT;
    surface.usage = VRO_ENGINE_SURFACE_SAMPLED |
                    VRO_ENGINE_SURFACE_EXTERNAL_PRODUCER;
    surface.plane_count = 1;

    results.push_back(runBatched("surface_validate", 50000, [&](uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) {
            assert(viro_engine_surface_desc_validate(&surface) ==
                   VRO_ENGINE_STATUS_OK);
        }
    }));

    const uint64_t rss = peakRssBytes();

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "{\n";
    emitString("schema", "viro-engine-benchmark-v1");
    emitString("backend", backend);
    std::cout << "  \"abi_major\": " << abi.abi_major << ",\n";
    std::cout << "  \"abi_minor\": " << abi.abi_minor << ",\n";
    std::cout << "  \"peak_rss_bytes\": " << rss << ",\n";
    std::cout << "  \"fixtures\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto &r = results[i];
        std::cout << "    {\"name\": \"" << r.name
                  << "\", \"operations\": " << r.operations
                  << ", \"p50_ns_per_op\": " << r.p50_ns
                  << ", \"p95_ns_per_op\": " << r.p95_ns << "}";
        if (i + 1 != results.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
    return 0;
}
