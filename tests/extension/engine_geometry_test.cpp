#include "VROEngineGeometryABI.h"

#include <cstdlib>
#include <iostream>

namespace {
bool expect(bool c,const char *m){ if(!c) std::cerr<<"FAIL: "<<m<<"\n"; return c; }
}

int main() {
    bool ok=true;
    float vertices[6] = {0,0,0, 1,0,0};
    uint16_t indices[2] = {0,1};
    VROEngineVertexAttribute attrs[1] = {{
        VRO_ENGINE_VERTEX_POSITION,
        VRO_ENGINE_COMPONENT_FLOAT32,
        3,
        0
    }};

    VROEngineGeometryDesc desc{};
    desc.struct_size=sizeof(desc);
    desc.topology=VRO_ENGINE_TOPOLOGY_LINES;
    desc.vertex_stride_bytes=12;
    desc.vertex_count=2;
    desc.index_type=VRO_ENGINE_INDEX_UINT16;
    desc.index_count=2;
    desc.attribute_count=1;
    desc.attributes=attrs;
    desc.vertices={reinterpret_cast<const uint8_t*>(vertices),sizeof(vertices)};
    desc.indices={reinterpret_cast<const uint8_t*>(indices),sizeof(indices)};
    desc.geometry_id=42;
    desc.version=7;

    ok &= expect(viro_engine_geometry_validate(&desc)==VRO_ENGINE_STATUS_OK,"valid geometry");

    float patch[3]={2,0,0};
    VROEngineGeometryRangeUpdate update{};
    update.struct_size=sizeof(update);
    update.stream_kind=0;
    update.geometry_id=42;
    update.base_version=7;
    update.next_version=8;
    update.offset_bytes=12;
    update.bytes={reinterpret_cast<const uint8_t*>(patch),sizeof(patch)};
    ok &= expect(viro_engine_geometry_range_validate(&desc,&update)==VRO_ENGINE_STATUS_OK,
                 "valid vertex patch");

    update.offset_bytes=20;
    ok &= expect(viro_engine_geometry_range_validate(&desc,&update)==
                     VRO_ENGINE_STATUS_INVALID_ARGUMENT,
                 "overflow patch rejected");
    update.offset_bytes=12;
    update.base_version=6;
    ok &= expect(viro_engine_geometry_range_validate(&desc,&update)==
                     VRO_ENGINE_STATUS_INVALID_ARGUMENT,
                 "stale base version rejected");

    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
