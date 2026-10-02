#include "VROEngineBackendSelector.h"

#include <atomic>

namespace {
std::atomic<uint32_t> gPreferences[VRO_ENGINE_DOMAIN_COUNT];

bool validDomain(VROEngineDomain domain) {
    return domain >= VRO_ENGINE_DOMAIN_INPUT && domain < VRO_ENGINE_DOMAIN_COUNT;
}

bool validPreference(VROEngineBackendPreference preference) {
    return preference >= VRO_ENGINE_BACKEND_PREFERENCE_AUTO &&
           preference <= VRO_ENGINE_BACKEND_PREFERENCE_ESKIU;
}
}

extern "C" VROEngineStatusCode viro_engine_backend_preference_set(
    VROEngineDomain domain,
    VROEngineBackendPreference preference) {
    if (!validDomain(domain) || !validPreference(preference)) {
        return VRO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    gPreferences[domain].store(static_cast<uint32_t>(preference),
                               std::memory_order_relaxed);
    return VRO_ENGINE_STATUS_OK;
}

extern "C" VROEngineBackendPreference viro_engine_backend_preference_get(
    VROEngineDomain domain) {
    if (!validDomain(domain)) {
        return VRO_ENGINE_BACKEND_PREFERENCE_AUTO;
    }
    return static_cast<VROEngineBackendPreference>(
        gPreferences[domain].load(std::memory_order_relaxed));
}

extern "C" VROEngineBackendKind viro_engine_backend_resolve(
    VROEngineDomain domain,
    uint32_t available_backends) {
    if (!validDomain(domain)) {
        return VRO_ENGINE_BACKEND_UNKNOWN;
    }

    const auto preference = viro_engine_backend_preference_get(domain);
    if (preference == VRO_ENGINE_BACKEND_PREFERENCE_CPP) {
        return (available_backends & VRO_ENGINE_BACKEND_AVAILABLE_CPP)
            ? VRO_ENGINE_BACKEND_CPP
            : VRO_ENGINE_BACKEND_UNKNOWN;
    }
    if (preference == VRO_ENGINE_BACKEND_PREFERENCE_ESKIU) {
        return (available_backends & VRO_ENGINE_BACKEND_AVAILABLE_ESKIU)
            ? VRO_ENGINE_BACKEND_ESKIU
            : VRO_ENGINE_BACKEND_UNKNOWN;
    }

    // AUTO deliberately favors the reference backend until a production
    // promotion explicitly changes policy for a domain.
    if (available_backends & VRO_ENGINE_BACKEND_AVAILABLE_CPP) {
        return VRO_ENGINE_BACKEND_CPP;
    }
    if (available_backends & VRO_ENGINE_BACKEND_AVAILABLE_ESKIU) {
        return VRO_ENGINE_BACKEND_ESKIU;
    }
    return VRO_ENGINE_BACKEND_UNKNOWN;
}

extern "C" void viro_engine_backend_preferences_reset(void) {
    for (uint32_t i = 0; i < VRO_ENGINE_DOMAIN_COUNT; ++i) {
        gPreferences[i].store(VRO_ENGINE_BACKEND_PREFERENCE_AUTO,
                              std::memory_order_relaxed);
    }
}
