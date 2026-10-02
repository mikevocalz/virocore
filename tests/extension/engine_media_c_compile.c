#include "VROEngineMediaABI.h"

_Static_assert(sizeof(VROEngineMediaPlane) == VRO_ENGINE_MEDIA_PLANE_V0_1_SIZE,
               "media plane C ABI changed");
_Static_assert(sizeof(VROEngineMediaFrame) == VRO_ENGINE_MEDIA_FRAME_V0_1_SIZE,
               "media frame C ABI changed");
_Static_assert(sizeof(VROEngineMediaMetrics) == VRO_ENGINE_MEDIA_METRICS_V0_1_SIZE,
               "media metrics C ABI changed");

int viro_media_c_compile_probe(void) {
    VROEngineMediaFrame frame = {0};
    frame.struct_size = sizeof(frame);
    return (int)frame.struct_size;
}
