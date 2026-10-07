//
//  VROSceneRendererMetalOpenXR.h
//
//  Desktop (macOS) OpenXR scene renderer bound through XR_KHR_metal_enable.
//  Mirrors the session/frame-loop logic of
//  android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.cpp, but without
//  JNI, EGL, or the Android VROSceneRenderer base class. Frame calls and
//  AppKit event pumping run on the calling thread — Meta XR Simulator's
//  in-process debug window requires frame submission from the main thread.
//
//  SOT-KEYWORDS: openxr, metal, macos, simulator, scene-renderer
//

#ifndef VROSceneRendererMetalOpenXR_h
#define VROSceneRendererMetalOpenXR_h

#include <memory>

class VROSceneRendererMetalOpenXR {
public:

    VROSceneRendererMetalOpenXR();
    ~VROSceneRendererMetalOpenXR();

    // Runs instance creation, session binding, the state machine, and the
    // blocking frame loop on the CALLING thread (must be the main thread —
    // the simulator's in-process debug window is pumped from here).
    // Returns 0 on success (frame budget reached, clean teardown), 1 on
    // failure. Frame budget defaults to VIRO_FRAMES env or 600.
    int run();

private:

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

#endif /* VROSceneRendererMetalOpenXR_h */
