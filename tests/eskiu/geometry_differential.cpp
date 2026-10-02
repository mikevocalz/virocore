#include "VROEngineGeometryABI.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" {
int viro_eskiu_geometry_validate(VROEngineGeometryDesc *desc);
int viro_eskiu_geometry_range_validate(
    VROEngineGeometryDesc *desc,
    VROEngineGeometryRangeUpdate *update);
}

namespace {

void expectSame(VROEngineGeometryDesc desc, const char *label) {
    const int cppStatus = static_cast<int>(viro_engine_geometry_validate(&desc));
    const int eskiuStatus = viro_eskiu_geometry_validate(&desc);
    if (cppStatus != eskiuStatus) {
        std::cerr << "FAIL " << label << ": cpp=" << cppStatus
                  << " eskiu=" << eskiuStatus << "\n";
        std::exit(EXIT_FAILURE);
    }
}

void expectRangeSame(
    VROEngineGeometryDesc desc,
    VROEngineGeometryRangeUpdate update,
    const char *label) {
    const int cppStatus = static_cast<int>(
        viro_engine_geometry_range_validate(&desc, &update));
    const int eskiuStatus =
        viro_eskiu_geometry_range_validate(&desc, &update);
    if (cppStatus != eskiuStatus) {
        std::cerr << "FAIL " << label << ": cpp=" << cppStatus
                  << " eskiu=" << eskiuStatus << "\n";
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    VROEngineVertexAttribute attrs[] = {
        {VRO_ENGINE_VERTEX_POSITION, VRO_ENGINE_COMPONENT_FLOAT32, 3, 0},
        {VRO_ENGINE_VERTEX_NORMAL, VRO_ENGINE_COMPONENT_FLOAT32, 3, 12},
    };
    float vertices[] = {
        0,0,0, 0,0,1,
        1,0,0, 0,0,1,
        0,1,0, 0,0,1,
    };
    uint32_t indices[] = {0,1,2};

    VROEngineGeometryDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.topology = VRO_ENGINE_TOPOLOGY_TRIANGLES;
    desc.vertex_stride_bytes = sizeof(float) * 6;
    desc.vertex_count = 3;
    desc.index_type = VRO_ENGINE_INDEX_UINT32;
    desc.index_count = 3;
    desc.attribute_count = 2;
    desc.attributes = attrs;
    desc.vertices = {
        reinterpret_cast<const uint8_t *>(vertices), sizeof(vertices)
    };
    desc.indices = {
        reinterpret_cast<const uint8_t *>(indices), sizeof(indices)
    };
    desc.geometry_id = 42;
    desc.version = 7;

    expectSame(desc, "valid");

    auto bad = desc;
    bad.struct_size = 1;
    expectSame(bad, "small descriptor");

    bad = desc;
    bad.topology = 99;
    expectSame(bad, "bad topology");

    bad = desc;
    bad.vertex_stride_bytes = 0;
    expectSame(bad, "zero stride");

    bad = desc;
    bad.vertices.length = 1;
    expectSame(bad, "short vertex view");

    bad = desc;
    VROEngineVertexAttribute badAttrs[] = {attrs[0], attrs[1]};
    badAttrs[1].offset_bytes = desc.vertex_stride_bytes;
    bad.attributes = badAttrs;
    expectSame(bad, "attribute beyond stride");

    bad = desc;
    bad.index_type = 99;
    expectSame(bad, "bad index type");

    bad = desc;
    bad.index_type = VRO_ENGINE_INDEX_NONE;
    bad.index_count = 3;
    expectSame(bad, "none index with count");

    VROEngineGeometryRangeUpdate update{};
    update.struct_size = sizeof(update);
    update.stream_kind = 0;
    update.geometry_id = desc.geometry_id;
    update.base_version = desc.version;
    update.next_version = desc.version + 1;
    update.offset_bytes = 0;
    update.bytes = {
        reinterpret_cast<const uint8_t *>(vertices),
        sizeof(float) * 6
    };
    expectRangeSame(desc, update, "valid vertex range");

    auto badUpdate = update;
    badUpdate.geometry_id += 1;
    expectRangeSame(desc, badUpdate, "wrong geometry id");

    badUpdate = update;
    badUpdate.next_version = badUpdate.base_version;
    expectRangeSame(desc, badUpdate, "non-advancing version");

    badUpdate = update;
    badUpdate.offset_bytes = desc.vertices.length;
    badUpdate.bytes.length = 1;
    expectRangeSame(desc, badUpdate, "range overflow");

    std::cout << "C++ / Eskiu geometry differential validation: PASS\n";
    return EXIT_SUCCESS;
}
