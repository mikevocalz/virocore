#include "VROEngineInputABI.h"

int viro_engine_input_c_header_probe(void) {
    VROEngineInputSample sample = {0};
    sample.struct_size = sizeof(sample);
    sample.kind = VRO_ENGINE_INPUT_GAZE;
    return sizeof(VROEngineInputSample) == 96 &&
           sample.struct_size == 96 &&
           VRO_ENGINE_INPUT_SAMPLE_V0_1_SIZE == 96;
}
