#include "VROEngineBackendSelector.h"
#include <cassert>
#include <iostream>

int main() {
    viro_engine_backend_preferences_reset();

    const uint32_t both = VRO_ENGINE_BACKEND_AVAILABLE_CPP |
                          VRO_ENGINE_BACKEND_AVAILABLE_ESKIU;

    assert(viro_engine_backend_resolve(VRO_ENGINE_DOMAIN_INPUT, both) ==
           VRO_ENGINE_BACKEND_CPP);

    assert(viro_engine_backend_preference_set(
               VRO_ENGINE_DOMAIN_INPUT,
               VRO_ENGINE_BACKEND_PREFERENCE_ESKIU) == VRO_ENGINE_STATUS_OK);
    assert(viro_engine_backend_resolve(VRO_ENGINE_DOMAIN_INPUT, both) ==
           VRO_ENGINE_BACKEND_ESKIU);

    assert(viro_engine_backend_resolve(
               VRO_ENGINE_DOMAIN_INPUT,
               VRO_ENGINE_BACKEND_AVAILABLE_CPP) == VRO_ENGINE_BACKEND_UNKNOWN);

    assert(viro_engine_backend_preference_set(
               VRO_ENGINE_DOMAIN_INPUT,
               VRO_ENGINE_BACKEND_PREFERENCE_CPP) == VRO_ENGINE_STATUS_OK);
    assert(viro_engine_backend_resolve(VRO_ENGINE_DOMAIN_INPUT, both) ==
           VRO_ENGINE_BACKEND_CPP);

    viro_engine_backend_preferences_reset();
    assert(viro_engine_backend_preference_get(VRO_ENGINE_DOMAIN_INPUT) ==
           VRO_ENGINE_BACKEND_PREFERENCE_AUTO);

    std::cout << "Engine backend selector: PASS\n";
    return 0;
}
