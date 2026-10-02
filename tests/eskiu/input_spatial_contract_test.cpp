#include "../../ViroRenderer/extensions/VROEngineInput.h"
#include "../../ViroRenderer/extensions/VROEngineSpatial.h"

#include <cassert>

int main() {
    VROEngineInputSample sample{};
    sample.struct_size = sizeof(VROEngineInputSample);
    sample.kind = VRO_ENGINE_INPUT_STYLUS;
    sample.pose.orientation[3] = 1.0f;

    VROEngineInputBatch batch{};
    batch.struct_size = sizeof(VROEngineInputBatch);
    batch.count = 1;
    batch.samples = &sample;

    assert(vro_engine_validate_input_batch(&batch).code == VRO_ENGINE_STATUS_OK);

    const auto identity = vro_engine_identity_transform(VRO_ENGINE_FRAME_SHARED, 42);
    assert(identity.struct_size == sizeof(VROEngineSpatialTransform));
    assert(identity.frame_kind == VRO_ENGINE_FRAME_SHARED);
    assert(identity.frame_id == 42);
    assert(identity.pose.orientation[3] == 1.0f);
    assert(identity.scale[0] == 1.0f);
    return 0;
}
