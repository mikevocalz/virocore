#include "VROOpenXRSimpleControllerBindings.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>

// Host-only test against the checked-in OpenXR headers. The "OpenXR binding
// host tests" step in .github/workflows/pull-request.yml compiles and runs it
// from android/ with -std=c++14 -Wall -Wextra -Werror. Keep backslashes off the
// end of // lines: GCC reports them as -Wcomment (multi-line comment).
int main() {
    auto action = [](uintptr_t value) { return reinterpret_cast<XrAction>(value); };
    const VROOpenXRSimpleControllerActions actions{
        {action(1), action(2)}, {action(3), action(4)},
        {action(5), action(6)}, {action(7), action(8)}, action(9)};
    std::map<XrPath, std::string> paths;
    const auto simple = VROOpenXRSimpleControllerBindings(actions, [&](const char *path) {
        const XrPath id = static_cast<XrPath>(paths.size() + 1);
        paths.emplace(id, path);
        return id;
    });
    const std::map<std::string, XrAction> allowed{
        {"/user/hand/left/input/aim/pose", actions.aim[0]},
        {"/user/hand/right/input/aim/pose", actions.aim[1]},
        {"/user/hand/left/input/grip/pose", actions.grip[0]},
        {"/user/hand/right/input/grip/pose", actions.grip[1]},
        {"/user/hand/left/input/select/click", actions.select[0]},
        {"/user/hand/right/input/select/click", actions.select[1]},
        {"/user/hand/left/input/menu/click", actions.menu},
        {"/user/hand/left/output/haptic", actions.haptic[0]},
        {"/user/hand/right/output/haptic", actions.haptic[1]}};
    std::set<std::string> seen;
    for (const auto &binding : simple) {
        const auto &path = paths.at(binding.binding);
        assert(allowed.count(path) == 1); // Rejects Touch-only components for this profile.
        assert(allowed.at(path) == binding.action); // Select preserves the existing trigger action.
        assert(seen.emplace(path).second);
    }
    assert(seen.size() == allowed.size());

    // A runtime accepts/rejects the complete profile suggestion. These are the
    // actual selector and list used by production, not another binding table.
    const XrActionSuggestedBinding fullController[] = {{action(10), 100}, {action(11), 101}};
    const auto selected = VROOpenXRBindingsForControllerProfile(
        "/interaction_profiles/khr/simple_controller", fullController, 2, simple);
    assert(selected.data == simple.data() && selected.count == 9);
    for (const char *profile : {"/interaction_profiles/oculus/touch_controller",
             "/interaction_profiles/bytedance/pico_neo3_controller",
             "/interaction_profiles/bytedance/pico4_controller",
             "/interaction_profiles/bytedance/pico4s_controller",
             "/interaction_profiles/bytedance/pico_g3_controller"}) {
        const auto unchanged = VROOpenXRBindingsForControllerProfile(profile, fullController, 2, simple);
        assert(unchanged.data == fullController && unchanged.count == 2);
    }
    return 0;
}
