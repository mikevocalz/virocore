#pragma once

#include <array>
#include <cstring>
#include <openxr/openxr.h>

// Existing actions, ordered left/right. select may use FLOAT_INPUT: OpenXR's
// Boolean-to-float conversion yields 0/1, not a measured pressure value.
struct VROOpenXRSimpleControllerActions {
    XrAction aim[2];
    XrAction grip[2];
    XrAction select[2];
    XrAction haptic[2];
    XrAction menu;
};

// Only components defined by /interaction_profiles/khr/simple_controller.
// Khronos hello_xr likewise binds a float grab action to select/click:
// https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/src/tests/hello_xr/openxr_program.cpp
// No trigger/value, squeeze, face buttons or thumbsticks exist in this profile.
template <typename MakePath>
inline std::array<XrActionSuggestedBinding, 9> VROOpenXRSimpleControllerBindings(
        const VROOpenXRSimpleControllerActions &actions, MakePath makePath) {
    return {{{actions.aim[0], makePath("/user/hand/left/input/aim/pose")},
             {actions.aim[1], makePath("/user/hand/right/input/aim/pose")},
             {actions.grip[0], makePath("/user/hand/left/input/grip/pose")},
             {actions.grip[1], makePath("/user/hand/right/input/grip/pose")},
             {actions.select[0], makePath("/user/hand/left/input/select/click")},
             {actions.select[1], makePath("/user/hand/right/input/select/click")},
             {actions.menu, makePath("/user/hand/left/input/menu/click")},
             {actions.haptic[0], makePath("/user/hand/left/output/haptic")},
             {actions.haptic[1], makePath("/user/hand/right/output/haptic")}}};
}

struct VROOpenXRControllerBindingList {
    const XrActionSuggestedBinding *data;
    uint32_t count;
};

inline VROOpenXRControllerBindingList VROOpenXRBindingsForControllerProfile(
        const char *profile, const XrActionSuggestedBinding *controllerBindings,
        uint32_t controllerBindingCount,
        const std::array<XrActionSuggestedBinding, 9> &simpleBindings) {
    if (std::strcmp(profile, "/interaction_profiles/khr/simple_controller") == 0) {
        return {simpleBindings.data(), static_cast<uint32_t>(simpleBindings.size())};
    }
    return {controllerBindings, controllerBindingCount};
}
