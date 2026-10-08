// VROInputControllerOpenXR.h
// ViroRenderer
//
// OpenXR input controller for Meta Quest Touch / Touch Plus controllers.
// Uses the OpenXR action-based input system:
//   - Actions are declared at session creation time (createActionSet)
//   - Bindings are suggested for /interaction_profiles/oculus/touch_controller
//   - Per-frame: xrSyncActions → query poses, buttons, axes
//
// M2 coverage:
//   Right controller  → ViroOculus::Controller  (primary ray, hover, trigger click)
//   Left controller   → ViroOculus::LeftController (pose + trigger click)
//   Right grip        → ViroOculus::RightGrip   (click on ≥0.5 squeeze)
//   Left grip         → ViroOculus::LeftGrip
//   A button (right)  → ViroOculus::AButton
//   B button (right)  → ViroOculus::BackButton  (back navigation, same as menu)
//   X button (left)   → ViroOculus::XButton
//   Y button (left)   → ViroOculus::YButton
//   Menu (left)       → ViroOculus::BackButton
//   Right thumbstick  → ViroOculus::RightThumbstick via onScroll
//   Left thumbstick   → ViroOculus::LeftThumbstick  via onScroll
//   Haptics           → triggerHaptic(session, hand, amplitude, durationSec)
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROINPUTCONTROLLEROPENXR_H
#define ANDROID_VROINPUTCONTROLLEROPENXR_H

#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <openxr/openxr.h>
#include "VROInputControllerBase.h"
#include "VROOpenXRRenderModel.h"

class VROInputPresenterOpenXR;

class VROInputControllerOpenXR : public VROInputControllerBase {
public:
    explicit VROInputControllerOpenXR(std::shared_ptr<VRODriver> driver)
        : VROInputControllerBase(driver) {}

    virtual ~VROInputControllerOpenXR();

    /*
     * Called once after xrCreateSession. Creates the action set, declares all
     * actions, suggests interaction profile bindings, and calls
     * xrAttachSessionActionSets. Must be called before the session enters
     * XR_SESSION_STATE_READY.
     */
    bool createActionSet(XrInstance instance, XrSession session,
                         bool eyeGazeSupported = false,
                         bool handInteractionSupported = false);

    /*
     * Destroy controller action spaces. Call before xrDestroySession.
     */
    void destroySpaces();

    /*
     * Initialize XR_EXT_hand_tracking. Loads function pointers, creates left/right
     * hand trackers. No-op (returns false) if the extension was not enabled at
     * instance creation time.
     *
     * @param aimExtAvail  true if XR_FB_hand_tracking_aim was also enabled
     */
    bool initHandTracking(XrInstance instance, XrSession session, bool aimExtAvail);

    /*
     * Destroy hand trackers. Call before xrDestroySession.
     */
    void destroyHandTrackers();

    /*
     * Enable runtime controller models (XR_FB_render_model). Call after
     * createActionSet() when the extension was enabled and the system reports
     * supportsRenderModelLoading. Without this call every hand uses the
     * fallback GLB. Render thread.
     */
    bool initRenderModels(XrInstance instance, XrSession session);

    /*
     * Stop render-model loads from touching the session. Blocks until a load in
     * flight returns. Call before xrDestroySession.
     */
    void destroyRenderModels();

    /*
     * Called every frame after prepareFrame (VRORenderer already set camera).
     * Syncs actions and emits Viro input events.
     *
     * @param session    Active XrSession
     * @param baseSpace  Reference space used for pose location (stage/local)
     * @param time       Predicted display time from XrFrameState
     * @param camera     Current Viro camera (for gaze / forward direction)
     */
    void onProcess(XrSession session, XrSpace baseSpace,
                   XrTime time, const VROCamera &camera);

    /*
     * Trigger haptic feedback on the given hand (0 = left, 1 = right).
     * No-op if the vibration action was not successfully created.
     */
    void triggerHaptic(XrSession session, int hand,
                       float amplitude = 0.5f, float durationSec = 0.05f);

    /*
     * Read the interaction profile the runtime has bound to /user/hand/left and
     * /user/hand/right and log it (ALOGI, "[XR-DIAG]" prefix), or "none bound
     * yet" when unbound. Also records whether each hand is bound to a hand
     * (not controller) profile, which hides that hand's controller model, and
     * asks the render-model path to re-check the connected controller. Call on
     * FOCUSED and on XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED. Uses the
     * instance captured in createActionSet() for xrPathToString.
     */
    void logActiveInteractionProfiles(XrSession session);

    VROVector3f getDragForwardOffset() override;

    std::string getHeadset()    override { return "quest"; }
    std::string getController() override { return "touch"; }

    /*
     * Set a callback invoked on the render thread when the B/Menu button is
     * pressed (ClickDown). Used to dispatch KEYCODE_BACK to the host Activity
     * so React Native's BackHandler fires in VRActivity.
     */
    void setBackButtonCallback(std::function<void()> callback) {
        _backButtonCallback = std::move(callback);
    }

protected:
    std::shared_ptr<VROInputPresenter> createPresenter(
        std::shared_ptr<VRODriver> driver) override;

    /*
     * Buttons ride their hand's aim ray: grip / A / thumbstick → Controller,
     * grip / X / Y / thumbstick → LeftController. BackButton is shared by B
     * and Menu, so it stays unmapped.
     */
    int rayForSource(int source) const override;

private:
    /*
     * Buttons and gestures are polled before this frame's hit update, so
     * their edges are queued and flushed at the end of onProcess.
     */
    void queueButtonEvent(int source, VROEventDelegate::ClickState state);
    std::vector<std::pair<int, VROEventDelegate::ClickState>> _pendingButtons;

    // ── Action set ────────────────────────────────────────────────────────────
    // Instance captured in createActionSet(); needed by xrStringToPath /
    // xrPathToString in logActiveInteractionProfiles(). Non-owning.
    XrInstance  _instance  = XR_NULL_HANDLE;
    XrActionSet _actionSet = XR_NULL_HANDLE;

    // ── Aim pose actions (one per hand) ──────────────────────────────────────
    // Aim drives the beam/hit ray; it tilts with the trigger pull.
    XrAction _leftAimPoseAction  = XR_NULL_HANDLE;
    XrAction _rightAimPoseAction = XR_NULL_HANDLE;

    // ── Grip pose actions (one per hand) ─────────────────────────────────────
    // Grip is the stable held-pose of the controller in the hand; it drives the
    // controller mesh (aim would make the mesh bob when the trigger is pulled).
    XrAction _leftGripPoseAction  = XR_NULL_HANDLE;
    XrAction _rightGripPoseAction = XR_NULL_HANDLE;

    // ── Trigger (float, per hand — click detected at ≥0.5) ───────────────────
    XrAction _leftTriggerAction  = XR_NULL_HANDLE;
    XrAction _rightTriggerAction = XR_NULL_HANDLE;

    // ── Grip / squeeze (float, per hand — click detected at ≥0.5) ────────────
    XrAction _leftGripAction  = XR_NULL_HANDLE;
    XrAction _rightGripAction = XR_NULL_HANDLE;

    // ── Face buttons ─────────────────────────────────────────────────────────
    XrAction _aButtonAction = XR_NULL_HANDLE;  // right hand A
    XrAction _bButtonAction = XR_NULL_HANDLE;  // right hand B  (→ BackButton)
    XrAction _xButtonAction = XR_NULL_HANDLE;  // left  hand X
    XrAction _yButtonAction = XR_NULL_HANDLE;  // left  hand Y
    XrAction _menuAction    = XR_NULL_HANDLE;  // left  hand Menu (→ BackButton)

    // ── Thumbstick axes (vector2f, per hand) ─────────────────────────────────
    XrAction _leftThumbstickAction  = XR_NULL_HANDLE;
    XrAction _rightThumbstickAction = XR_NULL_HANDLE;

    // ── Haptic output (one per hand) ─────────────────────────────────────────
    XrAction _leftVibrateAction  = XR_NULL_HANDLE;
    XrAction _rightVibrateAction = XR_NULL_HANDLE;

    // ── Action spaces for aim poses ───────────────────────────────────────────
    XrSpace _leftSpace  = XR_NULL_HANDLE;
    XrSpace _rightSpace = XR_NULL_HANDLE;

    // ── Action spaces for grip poses (drive the controller mesh) ──────────────
    XrSpace _leftGripSpace  = XR_NULL_HANDLE;
    XrSpace _rightGripSpace = XR_NULL_HANDLE;

    // ── Eye gaze (XR_EXT_eye_gaze_interaction; Quest Pro only) ────────────────
    // Additive input source: when the device reports eye-tracking support, the
    // gaze pose drives an extra ViroOculus::EyeGaze ray that feeds the same
    // hit-test / onHover path as the controllers. Disabled (no-op) otherwise.
    XrAction _eyeGazePoseAction = XR_NULL_HANDLE;
    XrSpace  _eyeGazeSpace      = XR_NULL_HANDLE;
    bool     _eyeGazeEnabled    = false;

    // ── Hand interaction (XR_EXT_hand_interaction; Android XR) ────────────────
    // pinch_ext/value (float, runtime-thresholded) binds to its own boolean
    // actions rather than the float trigger action. Its edge state is also
    // separate from the skeletal-tracking pinch flags —
    // two writers on one flag would corrupt updateInputButton edge detection.
    XrAction _leftPinchAction      = XR_NULL_HANDLE;
    XrAction _rightPinchAction     = XR_NULL_HANDLE;
    bool     _handInteractionEnabled = false;
    bool     _prevHandPinchLeft    = false;
    bool     _prevHandPinchRight   = false;
    // ── Select ownership (look to target, pinch to select) ────────────────────
    // Controller-less devices with an eye tracker (Meta VR glasses) send no
    // hover and no controller: with no aim action active and a located gaze,
    // a pinch on either hand selects what the eyes are on (EyeGaze source).
    // Without gaze (Quest 3/3S, tracking lost) the hand-aim ray keeps it.
    enum class SelectOwner { None, Controller, Hand, Gaze };
    SelectOwner _selectOwner   = SelectOwner::None;  // last logged, for change logs
    bool        _prevPinchGaze = false;              // merged two-hand pinch edge
    // Hands pinching in the current gaze press [left, right]; one leaving
    // tracking cancels the press instead of releasing it as a click.
    bool        _gazePressHand[2] = { false, false };
    // False while a pinch begun under gaze is still held, so it cannot fire a
    // fresh ClickDown on the hand ray when select falls back to the hand.
    bool        _pinchArmedLeft  = true;
    bool        _pinchArmedRight = true;
    // debug.viro.fake_gaze=1 (debug builds only): head pose stands in for a
    // located gaze so the gaze-select branch runs without an eye tracker.
    // Unconditional member so the class layout does not depend on NDEBUG.
    bool        _fakeGaze      = false;

    // ── Back button callback ──────────────────────────────────────────────────
    std::function<void()> _backButtonCallback;

    // ── Edge-detection state (previous frame) ────────────────────────────────
    bool _prevTriggerLeft  = false;
    bool _prevTriggerRight = false;
    bool _prevGripLeft     = false;
    bool _prevGripRight    = false;
    bool _prevAButton      = false;
    bool _prevBButton      = false;
    bool _prevXButton      = false;
    bool _prevYButton      = false;
    bool _prevMenuButton   = false;

    // ── Hand tracking (XR_EXT_hand_tracking) ─────────────────────────────────
    PFN_xrCreateHandTrackerEXT  _pfnCreateHandTracker  = nullptr;
    PFN_xrDestroyHandTrackerEXT _pfnDestroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT   _pfnLocateHandJoints   = nullptr;

    XrHandTrackerEXT _leftHandTracker  = XR_NULL_HANDLE;
    XrHandTrackerEXT _rightHandTracker = XR_NULL_HANDLE;
    bool             _aimExtEnabled    = false;
    bool             _handTrackingEnabled = true;

    // Per-hand gesture state (edge detection)
    bool _prevPinchLeft  = false;
    bool _prevPinchRight = false;
    bool _prevGrabLeft   = false;
    bool _prevGrabRight  = false;
    // Left-palm menu pinch (XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB). Left
    // hand only: the right-palm gesture is the OS system menu.
    bool _prevMenuGestureLeft = false;

    // ── Pose hysteresis (B18) ─────────────────────────────────────────────────
    // OpenXR pose probes (`xrLocateSpace`, FB hand-aim) routinely report
    // invalid for one or two frames during normal use (occlusion, IMU
    // settling, controller-hand handoff). Without smoothing, the aim source
    // flips between controller and synthesized hand pose every other frame,
    // producing rapid hover ENTER/EXIT oscillation and missed clicks. We
    // cache each side+source's last known good pose and re-use it for up
    // to `kMaxStaleFrames` frames before declaring the source unavailable.
    struct PersistentAim {
        bool          haveCached    = false;
        VROVector3f   pos;
        VROQuaternion rot;
        VROVector3f   forward;
        int           framesStale   = 0;
    };
    static constexpr int kMaxStaleFrames = 5;
    PersistentAim _rightCtrlAim;
    PersistentAim _leftCtrlAim;
    PersistentAim _rightHandAim;
    PersistentAim _leftHandAim;
    // Bridges blinks, which drop the gaze pose for a few frames; without it a
    // blink would hand selection to the hand ray in the middle of a look.
    PersistentAim _eyeGazeAim;

    /**
     * Apply hysteresis to a single source's pose. If `currentValid`, refreshes
     * the cache and returns true. Otherwise returns the cached pose for up to
     * `kMaxStaleFrames` frames so the aim doesn't flip to a different source
     * on a single bad probe. Returns false (and leaves out-params untouched)
     * once the cache has aged out.
     */
    static bool stickyPose(PersistentAim &state, bool currentValid,
                           VROVector3f &pos, VROQuaternion &rot,
                           VROVector3f &forward);

    // ── Private helpers ───────────────────────────────────────────────────────
    XrAction createAction(XrActionSet actionSet, XrActionType type,
                          const char *name, const char *localizedName);
    bool createActionSpaces(XrSession session);

    /**
     * Process hand-tracking joints: fire pinch/grab gesture events and emit
     * the per-hand aim pose via out-params. Hit-testing, processGazeEvent and
     * the laser update are dispatched in `onProcess` (once per source, for
     * both right and left independently). This method only collects state.
     *
     * `skipRight` / `skipLeft` are true when the matching controller already
     * produced a valid aim this frame — in that case we still run gesture
     * detection (pinch, grab) but do not collect the hand's aim pose.
     *
     * `gazeOwnsSelect` stops pinches from clicking on the hand rays; each
     * hand's pinch goes to `gazePinchOut[hand]` (0 = left, 1 = right) for the
     * merged EyeGaze edge instead. `gazeHandTrackedOut[hand]` is false when
     * that hand did not reach pinch detection this frame.
     */
    void processHands(XrSpace baseSpace, XrTime time, const VROCamera &camera,
                      bool skipRight, bool skipLeft,
                      bool gazeOwnsSelect, bool (&gazePinchOut)[2],
                      bool (&gazeHandTrackedOut)[2],
                      bool &rightAimValidOut,
                      VROVector3f &rightAimPosOut,
                      VROQuaternion &rightAimRotOut,
                      VROVector3f &rightAimForwardOut,
                      bool &leftAimValidOut,
                      VROVector3f &leftAimPosOut,
                      VROQuaternion &leftAimRotOut,
                      VROVector3f &leftAimForwardOut);

    /**
     * Push the aim ray for one source to the input presenter so it can render
     * (or hide) that source's laser line. Each source keeps its own polyline,
     * so left and right can be visible simultaneously.
     *
     * @param source   ViroOculus source ID (Controller for right, LeftController for left)
     * @param origin   World-space origin (ignored when !visible).
     * @param forward  Unit forward direction (ignored when !visible).
     *                 Hidden if magnitude < epsilon.
     * @param visible  False hides; true uses this source's `_hitResultsBySource`
     *                 entry as the endpoint, falling back to a fixed forward
     *                 distance when the source has no hit.
     */
    void updateLaserViz(int source, const VROVector3f &origin,
                        const VROVector3f &forward, bool visible);

    /**
     * Locate this hand's grip pose (via its grip action space) and position the
     * controller mesh there, hiding it when the grip pose action is inactive
     * (controller set down / hand tracking). Grip, not aim, so the mesh does not
     * bob when the trigger is pulled. No-op when the presenter has no mesh.
     */
    void updateControllerMeshViz(int source, XrSession session, XrSpace baseSpace,
                                 XrTime time, XrAction gripPoseAction, XrSpace gripSpace);

public:
    /** Enable or disable hand tracking gesture processing. Thread-safe (atomic store). */
    void setHandTrackingEnabled(bool enabled) { _handTrackingEnabled = enabled; }
    /*
     Whether the virtual controller model is drawn. Off in passthrough: the
     user already sees their real controllers, and the model covers them.
     */
    void setControllerMeshEnabled(bool enabled) { _controllerMeshEnabled = enabled; }
private:
    bool _controllerMeshEnabled = true;

    // ── Controller model source, per hand (0 = left, 1 = right) ───────────────
    // Order: the runtime render model (XR_FB_render_model) → the fallback GLB,
    // and the fallback when the runtime has no model path for that hand.
    // A runtime that lists the path but reports the model unavailable keeps the
    // hand hidden and is retried. If that lasts kMaxUnavailableFrames while the
    // controller's grip pose is active (an app without
    // com.oculus.permission.RENDER_MODEL can land here), the hand gets the
    // fallback mesh and keeps polling: a runtime model that shows up later
    // replaces it.
    struct ControllerModelState {
        enum class Phase { Unresolved, Loading, Runtime, Fallback };
        Phase              phase      = Phase::Unresolved;
        XrRenderModelKeyFB key        = XR_NULL_RENDER_MODEL_KEY_FB;
        uint32_t           generation = 0;     // bumps per load; stale completions drop
        uint64_t           nextAttemptFrame = 0;
        uint64_t           unavailableSinceFrame = 0;  // start of the current unavailable run
        bool               recheck    = false; // profile changed: re-query the key
        bool               retryLater = false; // set by a failed async load
        bool               fallbackUpgradable = false; // Fallback from timeout: keep polling
        bool               loggedUnavailable = false;  // also marks an unavailable run
        bool               hasModel   = false; // a GLB was handed to the presenter: draw it
        uint8_t            loadFailures = 0;   // hard failures; kMaxModelFailures → fallback
    };
    // Shared so an async load's completion (render thread) can update the state
    // without holding the controller alive.
    std::shared_ptr<ControllerModelState> _controllerModel[2] = {
        std::make_shared<ControllerModelState>(), std::make_shared<ControllerModelState>()
    };
    std::unique_ptr<VROOpenXRRenderModels> _renderModels;  // null: extension off
    std::string _renderModelCacheDir;
    uint64_t    _meshFrame = 0;
    // True while the hand is bound to a hand-tracking profile
    // (ext/hand_interaction_ext): the controller is not in that hand.
    bool _handProfileBound[2] = { false, false };

    /*
     * Decide (or re-check) where `hand`'s controller model comes from and start
     * loading it. Cheap when already resolved. Render thread.
     */
    void resolveControllerModel(int hand, int source,
                                const std::shared_ptr<VROInputPresenterOpenXR> &presenter);
    // Static: also called from async completions that must not touch `this`.
    static void applyFallbackModel(const std::shared_ptr<ControllerModelState> &state,
                                   int hand, int source, const char *reason,
                                   const std::shared_ptr<VROInputPresenterOpenXR> &presenter);
    static void onRuntimeModelFailure(const std::shared_ptr<ControllerModelState> &state,
                                      int hand, int source, const char *reason,
                                      const std::shared_ptr<VROInputPresenterOpenXR> &presenter);
    /*
     Wall-clock of the last camera-transform event handed to JS. The pose is
     consumed as a placement latch and a "pose alive" heartbeat — both work at
     10 Hz — while the event itself crosses JNI and the React bridge, so
     emitting it at display rate (72–90 Hz) was pure per-frame bridge churn.
     */
    double _lastCameraNotifyMs = -1.0;
};

#endif  // ANDROID_VROINPUTCONTROLLEROPENXR_H
