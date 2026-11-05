#pragma once

#include <string>
#include <vector>
#include <openxr/openxr.h>

static constexpr const char *kVROOpenXRLogitechStylusProfile =
    "/interaction_profiles/logitech/mx_ink_stylus_logitech";

// Actions ordered left/right. tipPose and tipForce are the stylus-only actions;
// a null entry drops its binding, matching a failed createAction.
struct VROOpenXRLogitechStylusActions {
    XrAction aim[2];
    XrAction grip[2];
    XrAction select[2];
    XrAction haptic[2];
    XrAction tipPose[2];
    XrAction tipForce[2];
};

// Bindings for the XR_LOGITECH_mx_ink_stylus_interaction profile, or none when
// the extension is not enabled. Once this profile is suggested the runtime
// binds a held stylus to it instead of the Touch profile, so any action left
// unbound here goes dead on that hand. Select maps to both the tip force and
// the front cluster button (OpenXR keeps the larger value), which feeds the
// trigger's pollFloatButton threshold; haptics map to the stylus motor.
template <typename MakePath>
inline std::vector<XrActionSuggestedBinding> VROOpenXRLogitechStylusBindings(
        bool extensionEnabled, const VROOpenXRLogitechStylusActions &actions,
        MakePath makePath) {
    std::vector<XrActionSuggestedBinding> bindings;
    if (!extensionEnabled) {
        return bindings;
    }
    const char *hands[] = {"/user/hand/left", "/user/hand/right"};
    for (int i = 0; i < 2; ++i) {
        const std::string prefix = hands[i];
        bindings.push_back({actions.aim[i], makePath((prefix + "/input/aim/pose").c_str())});
        bindings.push_back({actions.grip[i], makePath((prefix + "/input/grip/pose").c_str())});
        bindings.push_back({actions.select[i], makePath((prefix + "/input/tip_logitech/force").c_str())});
        bindings.push_back({actions.select[i], makePath((prefix + "/input/cluster_front_logitech/click").c_str())});
        bindings.push_back({actions.haptic[i], makePath((prefix + "/output/haptic").c_str())});
        if (actions.tipPose[i]) {
            bindings.push_back({actions.tipPose[i], makePath((prefix + "/input/tip_logitech/pose").c_str())});
        }
        if (actions.tipForce[i]) {
            bindings.push_back({actions.tipForce[i], makePath((prefix + "/input/tip_logitech/force").c_str())});
        }
    }
    return bindings;
}
