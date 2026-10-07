//
//  VROInputControllerXR.h
//
//  Desktop OpenXR input controller for the Meta XR Simulator harness.
//  Replaces the original header-only stub: it owns a real OpenXR action
//  set (eye-gaze pose via XR_EXT_eye_gaze_interaction, pinch via the
//  XR_EXT_hand_interaction profile) plus XR_EXT_hand_tracking trackers,
//  and translates what it reads into Viro input events
//  (hit-test/hover/click) through VROInputControllerBase.
//
//  Lifecycle (driven by VROSceneRendererMetalOpenXR):
//    bindXR(instance, session, stageSpace)   — once, while session is IDLE
//    setSessionState(state)                  — on SESSION_STATE_CHANGED
//    setFrameTime(predictedDisplayTime)      — every frame after xrWaitFrame
//    VRORenderer::prepareFrame() then calls onProcess(camera) per frame.
//
//  SOT-KEYWORDS: input, openxr, macos, hand-tracking, eye-gaze
//

#ifndef VROInputControllerXR_h
#define VROInputControllerXR_h

#include <openxr/openxr.h>
#include "VROInputControllerBase.h"
#include "VROVector3f.h"

class VROInputControllerXR : public VROInputControllerBase {
public:

    VROInputControllerXR(std::shared_ptr<VRODriver> driver);
    virtual ~VROInputControllerXR();

    /*
     Creates the action set, suggested bindings, action spaces and hand
     trackers. Must be called after xrCreateSession while the session is
     still in XR_SESSION_STATE_IDLE (xrAttachSessionActionSets requirement),
     and before any setFrameTime/onProcess calls.
     */
    void bindXR(XrInstance instance, XrSession session, XrSpace baseSpace);

    /*
     Tears down hand trackers, action space and action set. Must be called
     before xrDestroySession.
     */
    void shutdownXR();

    /*
     Called on every XrEventDataSessionStateChanged so xrSyncActions is only
     issued while the session is FOCUSED (per spec, sync outside FOCUSED is
     an error and action states stay inactive).
     */
    void setSessionState(XrSessionState state) { _sessionState = state; }

    /*
     The frame's predictedDisplayTime, used for xrLocateSpace /
     xrLocateHandJointsEXT inside onProcess.
     */
    void setFrameTime(XrTime time) { _frameTime = time; }

    /*
     Invoked once per frame by VRORenderer::prepareFrame. Syncs actions,
     locates the eye-gaze space, pushes the resulting ray through the Viro
     hit-test/hover pipeline, and maps pinch edges to click events.
     */
    void onProcess(const VROCamera &camera) override;

    std::string getHeadset() override { return "meta-xr-simulator"; }
    std::string getController() override { return "ext-hand-tracking+eye-gaze"; }

    // ---- Harness verification hooks (read-only) -----------------------------

    bool gazeValid()   const { return _gazeValid; }
    VROVector3f gazeOrigin()  const { return _gazeOrigin; }
    VROVector3f gazeForward() const { return _gazeForward; }
    bool pinchActive() const { return _pinchActive; }

    /*
     Which pointer ray fed the last hit test: 0=none, 1=eye-gaze action,
     2=hand-joint aim fallback.
     */
    int pointerSource() const { return _pointerSource; }

protected:

    VROVector3f getDragForwardOffset() override { return VROVector3f(0, 0, -1); }

    std::shared_ptr<VROInputPresenter> createPresenter(std::shared_ptr<VRODriver> driver) override;

private:

    XrInstance _instance = XR_NULL_HANDLE;
    XrSession  _session  = XR_NULL_HANDLE;
    XrSpace    _baseSpace = XR_NULL_HANDLE;
    XrSessionState _sessionState = XR_SESSION_STATE_UNKNOWN;
    XrTime     _frameTime = 0;

    // ---- Actions ------------------------------------------------------------
    XrActionSet  _actionSet  = XR_NULL_HANDLE;
    XrAction     _gazeAction = XR_NULL_HANDLE;   // pose, eye-gaze profile
    XrAction     _pinchAction[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE}; // float, L/R
    XrSpace      _gazeSpace  = XR_NULL_HANDLE;
    XrAction     _aimAction  = XR_NULL_HANDLE;   // pose, right controller/hand aim
    XrSpace      _aimSpace   = XR_NULL_HANDLE;
    bool         _actionsAttached = false;

    // ---- Hand tracking (XR_EXT_hand_tracking) -------------------------------
    PFN_xrLocateHandJointsEXT  _locateJoints = nullptr;
    PFN_xrDestroyHandTrackerEXT _destroyTracker = nullptr;
    XrHandTrackerEXT _handTracker[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool _handTrackingOk = false;

    // ---- Per-frame derived state --------------------------------------------
    bool        _gazeValid = false;
    VROVector3f _gazeOrigin, _gazeForward;
    bool        _pinchActive = false;
    bool        _handPinch[2] = {false, false};
    float       _lastJointPinchDist[2] = {-1.0f, -1.0f};
    float       _lastPinchValue[2] = {-1.0f, -1.0f};
    int         _pointerSource = 0;
    int         _framesProcessed = 0;

    XrAction createXrAction(XrActionType type, const char *name,
                          const char *localized);
    void processHands(const VROCamera &camera, bool &jointPinchOut,
                      VROVector3f &aimOrigin, VROVector3f &aimDir,
                      bool &aimValid);
};

#endif /* VROInputControllerXR_h */
