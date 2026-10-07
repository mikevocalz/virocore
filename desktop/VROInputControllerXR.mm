//
//  VROInputControllerXR.mm
//
//  Desktop OpenXR input controller: real action-based input for the
//  Meta XR Simulator harness. Structure follows the portable core of
//  android/sharedCode/.../VROInputControllerOpenXR.cpp (action set ->
//  suggested bindings -> attach -> per-frame xrSyncActions/xrLocateSpace)
//  with JNI/Android replaced by plain C++ logging.
//
//  Sources wired:
//    - XR_EXT_eye_gaze_interaction: pose action bound to
//      /user/eyes_ext/input/gaze_ext/pose under
//      /interaction_profiles/ext/eye_gaze_interaction; located each frame
//      via xrLocateSpace to produce the world-space gaze ray that feeds
//      VROInputControllerBase::updateHitNode/processGazeEvent.
//    - XR_EXT_hand_interaction: float actions bound to
//      /user/hand/{left,right}/input/pinch_ext/value under
//      /interaction_profiles/ext/hand_interaction_ext.
//    - XR_EXT_hand_tracking: L/R hand trackers, joints located each frame;
//      pinch is also derived from thumb-tip<->index-tip distance (<0.02m)
//      and, when the gaze action is unavailable, an aim ray derived from
//      the right-hand index proximal->tip direction is used as pointer.
//    - Right aim pose (/user/hand/right/input/aim/pose) on the touch
//      controller and hand-interaction profiles: the pointer on devices
//      without eye tracking (Quest 3), and on Glasses when gaze drops.
//
//  Pointer priority: eye gaze, then the aim action, then the joint ray.
//
//  Pinch = pinch_ext action (preferred) OR joint-distance fallback; both
//  signals are real runtime input and the log line records which fired.
//  Pinch edges map to Viro ClickDown/ClickUp on the gazed-at node, so the
//  box delegate sees a real OnClick event.
//
//  SOT-KEYWORDS: input, openxr, macos, hand-tracking, eye-gaze
//

#include "VROInputControllerXR.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#include "VROCamera.h"
#include "VROInputType.h"
#include "VROMatrix4f.h"
#include "VROQuaternion.h"

namespace {

// Pinch thresholds: pinch_ext/value is 0..1 (1 = fingers touching);
// joint distance is meters between thumb tip and index tip.
constexpr float kPinchActionThreshold = 0.5f;
constexpr float kPinchJointDistMeters = 0.02f;

VROVector3f xrVec(const XrVector3f &v) {
    return VROVector3f(v.x, v.y, v.z);
}

VROQuaternion xrQuat(const XrQuaternionf &q) {
    return VROQuaternion(q.x, q.y, q.z, q.w);
}

// -Z column of the pose rotation: OpenXR "look"/aim direction.
VROVector3f xrPoseForward(const XrPosef &pose) {
    VROMatrix4f m = xrQuat(pose.orientation).getMatrix();
    VROVector3f fwd(-m[8], -m[9], -m[10]);
    fwd.normalize();
    return fwd;
}

float jointDist(const XrHandJointLocationEXT &a, const XrHandJointLocationEXT &b) {
    if (!(a.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
        !(b.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        return -1.0f;
    }
    float dx = a.pose.position.x - b.pose.position.x;
    float dy = a.pose.position.y - b.pose.position.y;
    float dz = a.pose.position.z - b.pose.position.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

} // namespace

VROInputControllerXR::VROInputControllerXR(std::shared_ptr<VRODriver> driver) :
    VROInputControllerBase(driver) {}

VROInputControllerXR::~VROInputControllerXR() {
    shutdownXR();
}

std::shared_ptr<VROInputPresenter>
VROInputControllerXR::createPresenter(std::shared_ptr<VRODriver> driver) {
    // A real presenter matters: VROScene::attachInputController early-returns
    // when getPresenter() is null, which would leave _scene unset and neuter
    // updateHitNode/onButtonEvent. A plain VROInputPresenter supplies the
    // root node + no reticle — enough for the full event pipeline.
    return std::make_shared<VROInputPresenter>();
}

XrAction VROInputControllerXR::createXrAction(XrActionType type, const char *name,
                                              const char *localized) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE);
    std::strncpy(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    info.countSubactionPaths = 0;
    XrAction action = XR_NULL_HANDLE;
    XrResult r = xrCreateAction(_actionSet, &info, &action);
    if (XR_FAILED(r)) {
        std::printf("input-xr: xrCreateAction '%s' failed: %d\n", name, (int)r);
        return XR_NULL_HANDLE;
    }
    return action;
}

void VROInputControllerXR::bindXR(XrInstance instance, XrSession session,
                                  XrSpace baseSpace) {
    _instance = instance;
    _session = session;
    _baseSpace = baseSpace;

    auto toPath = [&](const char *str, XrPath *out) -> bool {
        XrResult r = xrStringToPath(instance, str, out);
        if (XR_FAILED(r)) {
            std::printf("input-xr: xrStringToPath '%s' failed: %d\n", str, (int)r);
            return false;
        }
        return true;
    };

    // ---- Action set ---------------------------------------------------------
    XrActionSetCreateInfo asi{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(asi.actionSetName, "viro_xr_input", XR_MAX_ACTION_SET_NAME_SIZE);
    std::strncpy(asi.localizedActionSetName, "Viro XR Input",
                 XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
    asi.priority = 0;
    if (XR_FAILED(xrCreateActionSet(instance, &asi, &_actionSet))) {
        std::puts("input-xr: xrCreateActionSet failed");
        return;
    }

    _gazeAction      = createXrAction(XR_ACTION_TYPE_POSE_INPUT,
                                      "eye_gaze", "Eye Gaze Pose");
    _pinchAction[0]  = createXrAction(XR_ACTION_TYPE_FLOAT_INPUT,
                                      "pinch_left", "Pinch Left");
    _pinchAction[1]  = createXrAction(XR_ACTION_TYPE_FLOAT_INPUT,
                                      "pinch_right", "Pinch Right");
    _aimAction       = createXrAction(XR_ACTION_TYPE_POSE_INPUT,
                                      "aim_right", "Aim Right");

    // ---- Suggested bindings -------------------------------------------------
    // Eye gaze profile: single component path under /user/eyes_ext.
    {
        XrPath profile, gazePath;
        if (toPath("/interaction_profiles/ext/eye_gaze_interaction", &profile) &&
            toPath("/user/eyes_ext/input/gaze_ext/pose", &gazePath) &&
            _gazeAction != XR_NULL_HANDLE) {
            XrActionSuggestedBinding binding{_gazeAction, gazePath};
            XrInteractionProfileSuggestedBinding s{
                XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            s.interactionProfile = profile;
            s.suggestedBindings = &binding;
            s.countSuggestedBindings = 1;
            XrResult r = xrSuggestInteractionProfileBindings(instance, &s);
            if (r == XR_ERROR_PATH_UNSUPPORTED) {
                std::puts("input-xr: eye gaze not supported by this system");
            } else {
                std::printf("input-xr: eye-gaze binding suggest -> %d\n", (int)r);
            }
        }
    }

    // EXT hand interaction profile: pinch_ext/value per hand.
    {
        XrPath profile, pl, pr, aim;
        if (toPath("/interaction_profiles/ext/hand_interaction_ext", &profile) &&
            toPath("/user/hand/left/input/pinch_ext/value", &pl) &&
            toPath("/user/hand/right/input/pinch_ext/value", &pr) &&
            toPath("/user/hand/right/input/aim/pose", &aim) &&
            _pinchAction[0] != XR_NULL_HANDLE && _pinchAction[1] != XR_NULL_HANDLE &&
            _aimAction != XR_NULL_HANDLE) {
            XrActionSuggestedBinding bindings[] = {
                {_pinchAction[0], pl},
                {_pinchAction[1], pr},
                {_aimAction, aim},
            };
            XrInteractionProfileSuggestedBinding s{
                XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            s.interactionProfile = profile;
            s.suggestedBindings = bindings;
            s.countSuggestedBindings = 3;
            XrResult r = xrSuggestInteractionProfileBindings(instance, &s);
            std::printf("input-xr: hand-interaction pinch binding suggest -> %d\n",
                        (int)r);
        }
    }

    // Touch controllers: the trigger drives the same pinch actions and the
    // right aim pose is the pointer, so a controller can select without hand
    // or eye tracking.
    for (const char *profileStr : {"/interaction_profiles/oculus/touch_controller",
                                   "/interaction_profiles/meta/touch_controller_plus"}) {
        XrPath profile, tl, tr, aim;
        if (toPath(profileStr, &profile) &&
            toPath("/user/hand/left/input/trigger/value", &tl) &&
            toPath("/user/hand/right/input/trigger/value", &tr) &&
            toPath("/user/hand/right/input/aim/pose", &aim) &&
            _pinchAction[0] != XR_NULL_HANDLE && _pinchAction[1] != XR_NULL_HANDLE &&
            _aimAction != XR_NULL_HANDLE) {
            XrActionSuggestedBinding bindings[] = {
                {_pinchAction[0], tl},
                {_pinchAction[1], tr},
                {_aimAction, aim},
            };
            XrInteractionProfileSuggestedBinding s{
                XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            s.interactionProfile = profile;
            s.suggestedBindings = bindings;
            s.countSuggestedBindings = 3;
            XrResult r = xrSuggestInteractionProfileBindings(instance, &s);
            std::printf("input-xr: %s trigger binding suggest -> %d\n",
                        profileStr, (int)r);
        }
    }

    // ---- Attach + action space ----------------------------------------------
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.actionSets = &_actionSet;
    attach.countActionSets = 1;
    XrResult ar = xrAttachSessionActionSets(session, &attach);
    _actionsAttached = XR_SUCCEEDED(ar);
    std::printf("input-xr: xrAttachSessionActionSets -> %d\n", (int)ar);

    if (_gazeAction != XR_NULL_HANDLE) {
        XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sci.action = _gazeAction;
        sci.subactionPath = XR_NULL_PATH;
        sci.poseInActionSpace.orientation = {0, 0, 0, 1};
        sci.poseInActionSpace.position = {0, 0, 0};
        XrResult r = xrCreateActionSpace(session, &sci, &_gazeSpace);
        std::printf("input-xr: gaze action space -> %d\n", (int)r);
    }
    if (_aimAction != XR_NULL_HANDLE) {
        XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sci.action = _aimAction;
        sci.subactionPath = XR_NULL_PATH;
        sci.poseInActionSpace.orientation = {0, 0, 0, 1};
        sci.poseInActionSpace.position = {0, 0, 0};
        XrResult r = xrCreateActionSpace(session, &sci, &_aimSpace);
        std::printf("input-xr: aim action space -> %d\n", (int)r);
    }

    // ---- Hand trackers (XR_EXT_hand_tracking) -------------------------------
    PFN_xrVoidFunction fn = nullptr;
    PFN_xrCreateHandTrackerEXT create = nullptr;
    xrGetInstanceProcAddr(instance, "xrCreateHandTrackerEXT", &fn);
    create = (PFN_xrCreateHandTrackerEXT)fn;
    xrGetInstanceProcAddr(instance, "xrDestroyHandTrackerEXT", &fn);
    _destroyTracker = (PFN_xrDestroyHandTrackerEXT)fn;
    xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT", &fn);
    _locateJoints = (PFN_xrLocateHandJointsEXT)fn;

    if (!create || !_destroyTracker || !_locateJoints) {
        std::puts("input-xr: hand_tracking entry points missing");
    } else {
        XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        ci.hand = XR_HAND_LEFT_EXT;
        XrResult rl = create(session, &ci, &_handTracker[0]);
        ci.hand = XR_HAND_RIGHT_EXT;
        XrResult rr = create(session, &ci, &_handTracker[1]);
        _handTrackingOk = XR_SUCCEEDED(rl) || XR_SUCCEEDED(rr);
        std::printf("input-xr: hand trackers L=%d R=%d\n", (int)rl, (int)rr);
    }
}

void VROInputControllerXR::shutdownXR() {
    if (_destroyTracker) {
        for (int h = 0; h < 2; ++h) {
            if (_handTracker[h] != XR_NULL_HANDLE) {
                _destroyTracker(_handTracker[h]);
                _handTracker[h] = XR_NULL_HANDLE;
            }
        }
    }
    if (_gazeSpace != XR_NULL_HANDLE) {
        xrDestroySpace(_gazeSpace);
        _gazeSpace = XR_NULL_HANDLE;
    }
    if (_aimSpace != XR_NULL_HANDLE) {
        xrDestroySpace(_aimSpace);
        _aimSpace = XR_NULL_HANDLE;
    }
    if (_actionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(_actionSet);
        _actionSet = XR_NULL_HANDLE;
    }
    _session = XR_NULL_HANDLE;
    _instance = XR_NULL_HANDLE;
    _baseSpace = XR_NULL_HANDLE;
    _actionsAttached = false;
    _handTrackingOk = false;
}

void VROInputControllerXR::onProcess(const VROCamera &camera) {
    if (_session == XR_NULL_HANDLE) return;
    ++_framesProcessed;

    // ---- Action sync: only legal while FOCUSED ------------------------------
    if (_actionsAttached && _sessionState == XR_SESSION_STATE_FOCUSED) {
        XrActiveActionSet active{_actionSet, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        XrResult sr = xrSyncActions(_session, &sync);
        static bool syncErrLogged = false;
        if (XR_FAILED(sr) && !syncErrLogged) {
            syncErrLogged = true;
            std::printf("input-xr: xrSyncActions failed: %d\n", (int)sr);
        }
    }

    // ---- Eye gaze ray --------------------------------------------------------
    _gazeValid = false;
    if (_actionsAttached && _gazeSpace != XR_NULL_HANDLE &&
        _sessionState == XR_SESSION_STATE_FOCUSED) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = _gazeAction;
        XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
        if (XR_SUCCEEDED(xrGetActionStatePose(_session, &gi, &ps)) && ps.isActive) {
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(_gazeSpace, _baseSpace, _frameTime, &loc)) &&
                (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                _gazeValid = true;
                _gazeOrigin = xrVec(loc.pose.position);
                _gazeForward = xrPoseForward(loc.pose);
            }
        }
    }

    // ---- Hands: joints -> pinch distance + aim fallback ----------------------
    bool jointPinch = false, aimValid = false;
    VROVector3f aimOrigin, aimDir;
    processHands(camera, jointPinch, aimOrigin, aimDir, aimValid);

    // ---- Aim action: controller or hand-interaction aim beats the joint ray --
    if (_actionsAttached && _aimSpace != XR_NULL_HANDLE &&
        _sessionState == XR_SESSION_STATE_FOCUSED) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = _aimAction;
        XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrGetActionStatePose(_session, &gi, &ps)) && ps.isActive &&
            XR_SUCCEEDED(xrLocateSpace(_aimSpace, _baseSpace, _frameTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            aimOrigin = xrVec(loc.pose.position);
            aimDir = xrPoseForward(loc.pose);
            aimValid = true;
        }
    }

    // ---- Pinch via pinch_ext actions (preferred signal) ----------------------
    bool actionPinch = false;
    if (_actionsAttached && _sessionState == XR_SESSION_STATE_FOCUSED) {
        for (int h = 0; h < 2; ++h) {
            if (_pinchAction[h] == XR_NULL_HANDLE) continue;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = _pinchAction[h];
            XrActionStateFloat fs{XR_TYPE_ACTION_STATE_FLOAT};
            if (XR_SUCCEEDED(xrGetActionStateFloat(_session, &gi, &fs)) && fs.isActive) {
                _lastPinchValue[h] = fs.currentState;
                if (fs.currentState >= kPinchActionThreshold) {
                    actionPinch = true;
                    _handPinch[h] = true;
                }
            }
        }
    }

    // ---- Pointer ray: gaze preferred, hand-aim fallback ----------------------
    _pointerSource = 0;
    VROVector3f rayOrigin, rayDir;
    if (_gazeValid) {
        rayOrigin = _gazeOrigin;
        rayDir = _gazeForward;
        _pointerSource = 1;
    } else if (aimValid) {
        rayOrigin = aimOrigin;
        rayDir = aimDir;
        _pointerSource = 2;
    }
    if (_pointerSource != 0) {
        VROInputControllerBase::updateHitNode(camera, rayOrigin, rayDir);
        VROInputControllerBase::onMove(ViroOculus::Controller, rayOrigin,
                                       VROQuaternion(), rayDir);
        VROInputControllerBase::processGazeEvent(ViroOculus::Controller);
    } else {
        // No ray this frame: drop the hit so a pinch can't click a stale target.
        _hitResult = nullptr;
    }

    // ---- Pinch edges -> Viro click events ------------------------------------
    bool pinched = actionPinch || jointPinch;
    if (pinched && !_pinchActive) {
        std::printf("input-xr: PINCH DOWN (action=%d joint=%d hit=%s)\n",
                    actionPinch ? 1 : 0, jointPinch ? 1 : 0,
                    _hitResult ? (_hitResult->isBackgroundHit() ? "background"
                                                              : "node") : "none");
        VROInputControllerBase::onButtonEvent(ViroOculus::Controller,
                                              VROEventDelegate::ClickState::ClickDown);
    } else if (!pinched && _pinchActive) {
        std::printf("input-xr: PINCH UP\n");
        VROInputControllerBase::onButtonEvent(ViroOculus::Controller,
                                              VROEventDelegate::ClickState::ClickUp);
    }
    _pinchActive = pinched;

    // ---- Periodic status ------------------------------------------------------
    if (_framesProcessed % 120 == 0) {
        std::printf("input-xr: state=%d src=%d gaze=%d o=(%.2f,%.2f,%.2f) f=(%.2f,%.2f,%.2f) "
                    "pinchVal L=%.2f R=%.2f jointDist L=%.3f R=%.3f hit=%s\n",
                    (int)_sessionState, _pointerSource, _gazeValid ? 1 : 0,
                    _gazeOrigin.x, _gazeOrigin.y, _gazeOrigin.z,
                    _gazeForward.x, _gazeForward.y, _gazeForward.z,
                    _lastPinchValue[0], _lastPinchValue[1],
                    _lastJointPinchDist[0], _lastJointPinchDist[1],
                    _hitResult ? (_hitResult->isBackgroundHit() ? "bg" : "node")
                               : "none");
    }
}

void VROInputControllerXR::processHands(const VROCamera &camera, bool &jointPinchOut,
                                        VROVector3f &aimOrigin, VROVector3f &aimDir,
                                        bool &aimValid) {
    jointPinchOut = false;
    aimValid = false;
    if (!_handTrackingOk || !_locateJoints) return;

    for (int h = 0; h < 2; ++h) {
        if (_handTracker[h] == XR_NULL_HANDLE) continue;

        XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
        std::memset(joints, 0, sizeof(joints));
        XrHandJointLocationsEXT locs{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locs.jointLocations = joints;
        XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        li.baseSpace = _baseSpace;
        li.time = _frameTime;

        XrResult r = _locateJoints(_handTracker[h], &li, &locs);
        if (XR_FAILED(r) || !locs.isActive) continue;

        // Pinch: thumb-tip <-> index-tip distance.
        float d = jointDist(joints[XR_HAND_JOINT_THUMB_TIP_EXT],
                            joints[XR_HAND_JOINT_INDEX_TIP_EXT]);
        _lastJointPinchDist[h] = d;
        if (d >= 0.0f && d < kPinchJointDistMeters) {
            jointPinchOut = true;
            _handPinch[h] = true;
        }

        // Aim fallback (right hand only): index proximal -> tip direction.
        if (h == 1) {
            const XrHandJointLocationEXT &tip = joints[XR_HAND_JOINT_INDEX_TIP_EXT];
            const XrHandJointLocationEXT &prox = joints[XR_HAND_JOINT_INDEX_PROXIMAL_EXT];
            if ((tip.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                (prox.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
                aimOrigin = xrVec(prox.pose.position);
                aimDir = xrVec(tip.pose.position) - aimOrigin;
                float len = aimDir.magnitude();
                if (len > 1e-4f) {
                    aimDir = aimDir.normalize();
                    aimValid = true;
                }
            }
        }
    }
}
