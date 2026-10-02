//
// VROEngineGeometryABI.h
//
// Borrowed geometry stream ABI for dynamic mesh/polyline/world-mesh adapters.
//

#ifndef VRO_ENGINE_GEOMETRY_ABI_H
#define VRO_ENGINE_GEOMETRY_ABI_H

#include <stdint.h>
#include "VROEngineABI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRO_ENGINE_GEOMETRY_ABI_MAJOR 0u
#define VRO_ENGINE_GEOMETRY_ABI_MINOR 1u

typedef enum VROEnginePrimitiveTopology {
    VRO_ENGINE_TOPOLOGY_POINTS = 1,
    VRO_ENGINE_TOPOLOGY_LINES = 2,
    VRO_ENGINE_TOPOLOGY_LINE_STRIP = 3,
    VRO_ENGINE_TOPOLOGY_TRIANGLES = 4,
    VRO_ENGINE_TOPOLOGY_TRIANGLE_STRIP = 5
} VROEnginePrimitiveTopology;

typedef enum VROEngineIndexType {
    VRO_ENGINE_INDEX_NONE = 0,
    VRO_ENGINE_INDEX_UINT16 = 1,
    VRO_ENGINE_INDEX_UINT32 = 2
} VROEngineIndexType;

typedef enum VROEngineVertexSemantic {
    VRO_ENGINE_VERTEX_POSITION = 1,
    VRO_ENGINE_VERTEX_NORMAL = 2,
    VRO_ENGINE_VERTEX_UV0 = 3,
    VRO_ENGINE_VERTEX_COLOR0 = 4,
    VRO_ENGINE_VERTEX_TANGENT = 5,
    VRO_ENGINE_VERTEX_CUSTOM0 = 16
} VROEngineVertexSemantic;

typedef enum VROEngineComponentType {
    VRO_ENGINE_COMPONENT_FLOAT32 = 1,
    VRO_ENGINE_COMPONENT_UINT8_NORM = 2,
    VRO_ENGINE_COMPONENT_UINT16 = 3,
    VRO_ENGINE_COMPONENT_UINT32 = 4
} VROEngineComponentType;

typedef struct VROEngineVertexAttribute {
    uint32_t semantic;
    uint32_t component_type;
    uint32_t component_count;
    uint32_t offset_bytes;
} VROEngineVertexAttribute;

#define VRO_ENGINE_VERTEX_ATTRIBUTE_V0_1_SIZE 16u

typedef struct VROEngineGeometryDesc {
    uint32_t struct_size;
    uint32_t topology;
    uint32_t vertex_stride_bytes;
    uint32_t vertex_count;
    uint32_t index_type;
    uint32_t index_count;
    uint32_t attribute_count;
    uint32_t flags;
    const VROEngineVertexAttribute *attributes;
    VROEngineByteView vertices;
    VROEngineByteView indices;
    uint64_t geometry_id;
    uint64_t version;
} VROEngineGeometryDesc;

#define VRO_ENGINE_GEOMETRY_DESC_V0_1_SIZE 88u

typedef struct VROEngineGeometryRangeUpdate {
    uint32_t struct_size;
    uint32_t stream_kind; /* 0 vertices, 1 indices */
    uint64_t geometry_id;
    uint64_t base_version;
    uint64_t next_version;
    uint64_t offset_bytes;
    VROEngineByteView bytes;
} VROEngineGeometryRangeUpdate;

#define VRO_ENGINE_GEOMETRY_RANGE_UPDATE_V0_1_SIZE 56u

VROEngineStatusCode viro_engine_geometry_validate(
    const VROEngineGeometryDesc *desc);

VROEngineStatusCode viro_engine_geometry_range_validate(
    const VROEngineGeometryDesc *desc,
    const VROEngineGeometryRangeUpdate *update);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(VROEngineVertexAttribute) == 16,
              "Vertex attribute ABI v0.1 must stay 16 bytes");
static_assert(sizeof(VROEngineGeometryDesc) == VRO_ENGINE_GEOMETRY_DESC_V0_1_SIZE,
              "Geometry descriptor ABI v0.1 must stay 88 bytes");
static_assert(sizeof(VROEngineGeometryRangeUpdate) ==
                  VRO_ENGINE_GEOMETRY_RANGE_UPDATE_V0_1_SIZE,
              "Geometry range ABI v0.1 must stay 56 bytes");
#endif

#endif
