#include "VROOpenXRLogitechStylusBindings.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>

// Host-only test against the checked-in OpenXR headers, run by the "OpenXR
// binding host tests" step in .github/workflows/pull-request.yml.
int main() {
    auto action = [](uintptr_t value) { return reinterpret_cast<XrAction>(value); };
    const VROOpenXRLogitechStylusActions actions{
        {action(1), action(2)}, {action(3), action(4)},
        {action(5), action(6)}, {action(7), action(8)},
        {action(9), action(10)}, {action(11), action(12)}};
    std::map<XrPath, std::string> paths;
    auto makePath = [&](const char *path) {
        const XrPath id = static_cast<XrPath>(paths.size() + 1);
        paths.emplace(id, path);
        return id;
    };

    // Extension disabled: nothing is suggested and no path is even resolved.
    assert(VROOpenXRLogitechStylusBindings(false, actions, makePath).empty());
    assert(paths.empty());

    // Extension enabled: exactly the MX Ink path set, per hand.
    const auto stylus = VROOpenXRLogitechStylusBindings(true, actions, makePath);
    std::set<std::pair<std::string, XrAction>> expected;
    const char *hands[] = {"/user/hand/left", "/user/hand/right"};
    for (int i = 0; i < 2; ++i) {
        const std::string h = hands[i];
        expected.insert({h + "/input/aim/pose", actions.aim[i]});
        expected.insert({h + "/input/grip/pose", actions.grip[i]});
        // Select (the trigger action) fires from the tip force and the front button.
        expected.insert({h + "/input/tip_logitech/force", actions.select[i]});
        expected.insert({h + "/input/cluster_front_logitech/click", actions.select[i]});
        expected.insert({h + "/output/haptic", actions.haptic[i]});
        expected.insert({h + "/input/tip_logitech/pose", actions.tipPose[i]});
        expected.insert({h + "/input/tip_logitech/force", actions.tipForce[i]});
    }
    std::set<std::pair<std::string, XrAction>> seen;
    for (const auto &binding : stylus) {
        const auto entry = std::make_pair(paths.at(binding.binding), binding.action);
        assert(expected.count(entry) == 1);
        assert(seen.insert(entry).second);
        // No Touch components leak into the stylus profile.
        assert(entry.first.find("trigger") == std::string::npos);
        assert(entry.first.find("thumbstick") == std::string::npos);
    }
    assert(seen == expected);
    assert(stylus.size() == 14);

    // A stylus-only action that failed to create drops only its own binding.
    VROOpenXRLogitechStylusActions partial = actions;
    partial.tipPose[1] = XR_NULL_HANDLE;
    partial.tipForce[0] = XR_NULL_HANDLE;
    const auto reduced = VROOpenXRLogitechStylusBindings(true, partial, makePath);
    assert(reduced.size() == 12);
    for (const auto &binding : reduced) {
        assert(binding.action != XR_NULL_HANDLE);
    }

    assert(std::string(kVROOpenXRLogitechStylusProfile) ==
           "/interaction_profiles/logitech/mx_ink_stylus_logitech");
    return 0;
}
