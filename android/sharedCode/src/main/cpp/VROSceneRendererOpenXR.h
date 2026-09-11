// VROSceneRendererOpenXR.h
// ViroRenderer
//
// Scene renderer for Meta Quest via the Khronos OpenXR API.
// Replaces the legacy VROSceneRendererOVR (VrApi) which is not supported
// on Quest 3 and newer hardware.
//
// Lifecycle managed by ViroViewOpenXR (Java) via JNI:
//   constructor  → xrCreateInstance, xrGetSystem, EGL context, xrCreateSession
//   onResume     → start render thread
//   onPause      → signal render thread to pause
//   onDestroy    → stop render thread, destroy session + instance
//
// Render thread loop:
//   xrPollEvent (session state machine)
//   xrWaitFrame / xrBeginFrame
//   for each eye: acquire swapchain → render → release
//   xrEndFrame with projection layer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROSCENERENDEREROPENBXR_H
#define ANDROID_VROSCENERENDEREROPENBXR_H

#include "VROSceneRenderer.h"
#include <memory>
#include <thread>
#include <atomic>
#include <vector>

// Must be defined before openxr_platform.h to enable OpenGL ES + Android types
// (XrSwapchainImageOpenGLESKHR, XrGraphicsBindingOpenGLESAndroidKHR, etc.)
#define XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_PLATFORM_ANDROID

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

class VRORendererConfiguration;
class VRODriverOpenGLAndroidOpenXR;
class VROInputControllerOpenXR;
class VRODisplayOpenGLOpenXR;
class VROARSessionOpenXR;
class VROSceneController;

namespace gvr { class AudioApi; }

// Per-eye swapchain state
struct VROOpenXRSwapchain {
    XrSwapchain                          handle   = XR_NULL_HANDLE;
    uint32_t                             width    = 0;
    uint32_t                             height   = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
};

class VROSceneRendererOpenXR : public VROSceneRenderer {
public:

    VROSceneRendererOpenXR(VRORendererConfiguration config,
                           std::shared_ptr<gvr::AudioApi> gvrAudio,
                           jobject view, jobject activity, JNIEnv *env);
    virtual ~VROSceneRendererOpenXR();

    // ── VROSceneRenderer interface ────────────────────────────────────────────
    void initGL()            {}  // OpenXR manages its own context; not used
    void onDrawFrame()       {}  // Render loop runs on dedicated thread; not used
    void onTouchEvent(int action, float x, float y);
    void onKeyEvent(int keyCode, int action);
    void setVRModeEnabled(bool enabled) {}
    void recenterTracking();

    // Wire up the Quest MR (AR) session when the scene is a VROARScene. For a
    // plain VR (VROScene) this behaves exactly like the base implementation.
    // Both overloads must be overridden: ViroViewOpenXR drives scene changes
    // through the timed (float seconds) variant.
    void setSceneController(std::shared_ptr<VROSceneController> sceneController) override;
    void setSceneController(std::shared_ptr<VROSceneController> sceneController, float seconds,
                            VROTimingFunctionType timingFunction) override;

    // ── OpenXR extensions (callable from Java via JNI) ────────────────────────
    /**
     * Enable or disable XR_FB_passthrough mixed-reality mode.
     * No-op if the extension is unavailable on the current device.
     * Safe to call from any thread — changes take effect on the next frame.
     */
    void setPassthroughEnabled(bool enabled);
    /**
     * Style the passthrough layer (XR_FB_passthrough). opacity is the texture
     * opacity factor [0,1]; edge[RGBA] is the edge-highlight colour (alpha 0
     * disables the edge effect). No-op if passthrough is unavailable.
     */
    void setPassthroughStyle(float opacity, float edgeR, float edgeG, float edgeB, float edgeA);
    void setHandTrackingEnabled(bool enabled);
    void onStart();
    void onResume();
    void onPause();
    void onStop();
    void onDestroy();
    void onSurfaceCreated(jobject surface)  {}  // OpenXR owns the display surface
    void onSurfaceChanged(jobject surface, VRO_INT w, VRO_INT h) {}
    void onSurfaceDestroyed() {}

private:

    // ── Runtime classification ────────────────────────────────────────────────
    // Populated once, immediately after xrCreateInstance succeeds, from
    // xrGetInstanceProperties + the negotiated extension set. Read-only after init.
    enum class VROOpenXRVendor { UNKNOWN, META, PICO, KHRONOS_OTHER };
    struct VROOpenXRRuntimeInfo {
        bool            valid              = false;
        VROOpenXRVendor vendor             = VROOpenXRVendor::UNKNOWN;
        char            runtimeName[XR_MAX_RUNTIME_NAME_SIZE] = {0};
        uint16_t        apiMajor           = 0;
        uint16_t        apiMinor           = 0;
        uint32_t        apiPatch           = 0;
        bool            androidCreateInstanceEnabled = false;
        bool            passthroughAvailable         = false;  // XR_FB_passthrough
        bool            displayRefreshRateAvailable  = false;  // XR_FB_display_refresh_rate
        bool            handTrackingAvailable        = false;  // XR_EXT_hand_tracking
        bool            handAimExtAvailable          = false;  // XR_FB_hand_tracking_aim
    };
    VROOpenXRRuntimeInfo _runtimeInfo;

public:
    const VROOpenXRRuntimeInfo &getRuntimeInfo() const { return _runtimeInfo; }

    // ── Foveation (XR_FB_foveation) ────────────────────────────────────────────
    enum class VROFoveationLevel { OFF = 0, LOW = 1, MEDIUM = 2, HIGH = 3 };
    // Apply a fixed-foveation level to both eye swapchains. dynamic=true lets the
    // runtime scale the level with GPU load. No-op (returns false) when
    // FB_foveation was not negotiated. Safe to call after session start.
    bool setFoveationLevel(VROFoveationLevel level, bool dynamic);
    bool isFoveationAvailable()           const { return _foveationAvailable; }
    bool isEyeTrackedFoveationAvailable() const { return _eyeTrackedFoveationAvailable; }

    // ── Tracking origin (eye-level vs floor-level) ─────────────────────────────
    // Eye:   XR_REFERENCE_SPACE_TYPE_LOCAL, origin at the head pose at session
    //        start. Every other Viro platform's convention; the upstream default.
    // Floor: origin on the physical floor, so a node at y=0 sits on the floor.
    //        Resolved via a ladder (see createReferenceSpace): native LOCAL_FLOOR
    //        when enumerated, else a LOCAL space offset by the STAGE floor height,
    //        else it stays Eye and reports so — never a hardcoded human height.
    enum class VROTrackingOrigin { Eye, Floor };
    // Request a tracking origin. Rebuilds the reference space when the session
    // is live; otherwise the value is cached and applied at session start. Safe
    // to call before the renderer exists (via the pending flag in ViroViewOpenXR).
    void setTrackingOrigin(VROTrackingOrigin origin);
    VROTrackingOrigin getTrackingOrigin() const { return _trackingOrigin; }

private:
    bool initFoveation();   // called once after swapchain creation
    // Build a reference space for `origin`, following the fallback ladder, into
    // `outSpace` and reporting the type actually created via `outType`. Returns
    // false only on an outright xrCreateReferenceSpace failure.
    bool buildReferenceSpace(VROTrackingOrigin origin, XrSpace *outSpace,
                             XrReferenceSpaceType *outType);
    // Locate the STAGE floor's Y offset below LOCAL at `time`. Returns false when
    // STAGE is not enumerated or its position is not locatable this frame.
    bool deriveFloorOffset(XrTime time, float *outOffsetY);

    bool _foveationAvailable            = false;  // XR_FB_foveation present + fns loaded
    bool _eyeTrackedFoveationAvailable  = false;  // XR_META_foveation_eye_tracked present
    bool _swapchainUpdateStateAvailable = false;  // XR_FB_swapchain_update_state present
    PFN_xrCreateFoveationProfileFB  _pfnCreateFoveationProfile  = nullptr;
    PFN_xrDestroyFoveationProfileFB _pfnDestroyFoveationProfile = nullptr;
    PFN_xrUpdateSwapchainFB         _pfnUpdateSwapchain         = nullptr;

    // ── OpenXR core handles ───────────────────────────────────────────────────
    XrInstance      _instance   = XR_NULL_HANDLE;
    XrSystemId      _systemId   = XR_NULL_SYSTEM_ID;
    XrSession       _session    = XR_NULL_HANDLE;
    // The reference space every subsystem (input, projection layer, plane
    // sources) resolves against. Its type follows _trackingOrigin: LOCAL for
    // Eye, LOCAL_FLOOR or an offset LOCAL for Floor. _appSpaceType records what
    // was actually created so recenter and the change-pending handler rebuild
    // like-for-like.
    XrSpace              _appSpace     = XR_NULL_HANDLE;
    XrReferenceSpaceType _appSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    VROTrackingOrigin    _trackingOrigin = VROTrackingOrigin::Eye;
    bool                 _localFloorAvailable = false;  // XR_EXT_local_floor negotiated
    // Y offset (metres, >= 0) of the physical floor below the LOCAL origin, used
    // by the STAGE-emulation rung. Re-derived at session start and on recenter.
    float                _floorOffsetY = 0.0f;

    XrSessionState  _sessionState             = XR_SESSION_STATE_UNKNOWN;
    XrTime          _lastPredictedDisplayTime = 0;  // updated each frame; used by recenterTracking()
    bool            _sessionRunning        = false;
    bool            _passthroughEnabled    = false;
    bool            _handTrackingAvailable = false;  // XR_EXT_hand_tracking present
    bool            _handAimExtAvailable   = false;  // XR_FB_hand_tracking_aim present
    bool            _planeDetectionAvailable = false;  // XR_EXT_plane_detection present
    bool            _fbSceneAvailable         = false;  // XR_FB_scene present
    bool            _fbSpatialEntityAvailable = false;  // XR_FB_spatial_entity present
    bool            _fbSpatialQueryAvailable  = false;  // XR_FB_spatial_entity_query present
    bool            _eyeGazeAvailable         = false;  // XR_EXT_eye_gaze_interaction present
    bool            _eyeGazeSupported         = false;  // system actually has eye tracking (Quest Pro)

    // Per-eye swapchains (index 0 = left, 1 = right)
    VROOpenXRSwapchain _swapchains[2];

    // Passthrough extension handles (XR_FB_passthrough)
    XrPassthroughFB      _passthrough      = XR_NULL_HANDLE;
    XrPassthroughLayerFB _passthroughLayer = XR_NULL_HANDLE;

    // Extension function pointers — loaded in initPassthrough() via xrGetInstanceProcAddr.
    // All are null until XR_FB_passthrough is confirmed available and initialised.
    PFN_xrCreatePassthroughFB       _pfnCreatePassthrough       = nullptr;
    PFN_xrDestroyPassthroughFB      _pfnDestroyPassthrough      = nullptr;
    PFN_xrPassthroughStartFB        _pfnPassthroughStart        = nullptr;
    PFN_xrPassthroughPauseFB        _pfnPassthroughPause        = nullptr;
    PFN_xrCreatePassthroughLayerFB  _pfnCreatePassthroughLayer  = nullptr;
    PFN_xrDestroyPassthroughLayerFB _pfnDestroyPassthroughLayer = nullptr;
    PFN_xrPassthroughLayerResumeFB  _pfnPassthroughLayerResume  = nullptr;
    PFN_xrPassthroughLayerPauseFB   _pfnPassthroughLayerPause   = nullptr;
    PFN_xrPassthroughLayerSetStyleFB _pfnPassthroughLayerSetStyle = nullptr;

    // ── EGL ──────────────────────────────────────────────────────────────────
    EGLDisplay  _eglDisplay  = EGL_NO_DISPLAY;
    EGLConfig   _eglConfig   = nullptr;
    EGLContext  _eglContext  = EGL_NO_CONTEXT;
    EGLSurface  _eglSurface  = EGL_NO_SURFACE;  // tiny pbuffer surface

    // ── Render thread ─────────────────────────────────────────────────────────
    std::thread       _renderThread;
    std::atomic<bool> _running  { false };
    std::atomic<bool> _paused   { true  };

    // ── Viro subsystems ───────────────────────────────────────────────────────
    // _openxrDriver holds the derived type for getOpenXRDisplay(); base class
    // _driver (VRODriverOpenGLAndroid) is also set to the same shared_ptr in the
    // constructor so that VROSceneRenderer methods (setSceneController, etc.) work.
    std::shared_ptr<VRODriverOpenGLAndroidOpenXR> _openxrDriver;
    std::shared_ptr<VROInputControllerOpenXR>     _inputController;
    std::shared_ptr<gvr::AudioApi>                _gvrAudio;
    jobject                                        _activity = nullptr;

    // Quest MR (AR) session — owns XR_EXT_plane_detection and feeds detected
    // planes into the standard VROARScene anchor pipeline. Null when plane
    // detection is unavailable on the device. Driven once per renderFrame().
    std::shared_ptr<VROARSessionOpenXR>           _arSession;

    // ── Java callback (onDrawFrame) ───────────────────────────────────────────
    JavaVM  *_jvm      = nullptr;
    jobject  _jview    = nullptr;  // global ref to ViroViewOpenXR instance

    // ── Init / teardown helpers ───────────────────────────────────────────────
    bool initOpenXR();           // xrCreateInstance, xrGetSystem
    bool createEGLContext();
    bool createSession();        // xrCreateSession with GLES binding
    bool createReferenceSpace();
    bool createSwapchains();
    bool initPassthrough();
    bool initHandTracking();     // XR_EXT_hand_tracking — no-op if extension unavailable
    void destroySwapchains();
    void destroySession();
    void destroyEGLContext();
    void destroyOpenXR();

    // ── Render thread ─────────────────────────────────────────────────────────
    // Enable passthrough + wire the AR session when sceneController holds a
    // VROARScene. No-op for plain VR scenes. Called from both setSceneController
    // overloads after the base attaches the scene.
    void attachARSceneIfNeeded(std::shared_ptr<VROSceneController> sceneController);

    void renderLoop();
    void pollEvents();
    void handleSessionStateChange(XrEventDataSessionStateChanged *event);
    void renderFrame();

    // ── Per-eye render ────────────────────────────────────────────────────────
    void renderEye(int eyeIndex,
                   const XrView &view,
                   VROOpenXRSwapchain &swapchain);
};

#endif  // ANDROID_VROSCENERENDEREROPENBXR_H
