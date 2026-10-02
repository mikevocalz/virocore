#include "VROEngineGeometryABI.h"
int viro_geometry_c_header_probe(void) {
    return sizeof(VROEngineVertexAttribute)==VRO_ENGINE_VERTEX_ATTRIBUTE_V0_1_SIZE &&
           sizeof(VROEngineGeometryDesc)==VRO_ENGINE_GEOMETRY_DESC_V0_1_SIZE &&
           sizeof(VROEngineGeometryRangeUpdate)==VRO_ENGINE_GEOMETRY_RANGE_UPDATE_V0_1_SIZE;
}
