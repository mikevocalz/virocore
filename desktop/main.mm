//
//  main.mm — viro_sim_host entry point.
//
//  Meta XR Simulator's in-process debug window requires AppKit event pumping
//  and OpenXR frame calls on the main thread, so this stays a minimal
//  NSApplication pump: no windows, no delegate, accessory activation policy.
//  The session/frame loop runs in VROSceneRendererMetalOpenXR::run() and
//  pumps pending NSEvents once per iteration.
//
//  Env: VIRO_FRAMES=<n> overrides the default 600-frame budget.
//
//  SOT-KEYWORDS: openxr, metal, macos, simulator, host-main
//

#import <AppKit/AppKit.h>
#include "VROSceneRendererMetalOpenXR.h"

int main(int argc, const char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
    }
    VROSceneRendererMetalOpenXR renderer;
    return renderer.run();
}
