# PR #121 description update: hand pinch state fixes

Replace the "Pinch detection" section of the PR #121 description with the text
below. Renderer-only change; no JS API change.

## Pinch detection

Each hand's pinch is tracked by `VROHandPinchState`
(`android/sharedCode/src/main/cpp/VROHandPinchState.h`), driven once per frame
from `VROInputControllerOpenXR::processHands`. Three review findings changed
how it behaves.

### 1. The press latches its metric and its ray

The pinch is measured one of two ways: FB aim pinch strength (0.75 press /
0.55 release) when `XR_HAND_TRACKING_AIM_VALID_BIT_FB` is set, or thumb-tip to
index-tip distance (20 mm press / 25 mm release) when it is not. Before, the
metric was re-chosen every frame and both fed one state machine, so a frame
where the aim bit flickered could release a strength press with a distance
reading, or open a press with no release in between. The ray origin moved with
it (aim pose vs wrist).

Now the metric that opens a press stays in force until that press ends. A
sample from the other metric during a press is an invalid sample (see 3), never
a reading. The ray stays on the latched metric's origin, so losing the FB aim
mid-press holds the last aim pose and then cancels; it never jumps to the wrist
ray. While released, the metric may change, but the new metric has to see an
open hand before it can arm.

Who this affects: anyone drawing or dragging with hands on Quest, where the FB
aim validity bit drops for a frame or two during normal use. Strokes no longer
end or restart on those frames, and the ray does not snap sideways.

### 2. Nothing fires until the hand has been seen open

`VROHandPinchState` starts disarmed and arms only after a released sample
(strength at or below 0.55, or distance at or above 25 mm). The same applies
after a hand comes back from inactive, after a cancel, and after
`setHandTrackingEnabled(true)`.

Who this affects: a hand that enters tracking already pinched (picking up the
headset, resuming from the system menu, re-enabling hand tracking while
pinching) no longer emits a phantom `ClickDown` on its first frame.

### 3. Dropouts get a short grace window; system gestures do not

A press now survives `VROHandPinchState::kInvalidSampleGraceFrames` (default
3) consecutive invalid samples: tracking inactive, joints invalid, aim lost,
non-finite values. During those frames the press stays down and the ray holds
its last valid pose. On the third invalid frame the press cancels, exactly
once, and disarms. Any valid sample inside the window resets the count.

`XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB` and
`XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB` cancel at once with no grace, as do
controller takeover of the side, `setHandTrackingEnabled(false)` and a missing
tracker. A stroke must not continue into the system UI.

Why 3: a cancelled press lifts the pen, so a one-frame dropout used to split a
stroke in two. Holding too long draws ink after the hand has opened. At
72-90 Hz, 3 frames is 33-42 ms. The constant is a candidate; it is not device
qualified.

How to dial it back: change `kInvalidSampleGraceFrames` in
`VROHandPinchState.h`. Set it to 1 to restore single-frame cancel (the previous
behaviour). Raise it if strokes still split on real hardware. The unit test
pins the default at 3 (`graceWindow()` in `VROHandPinchStateTest.cpp`); update
that check with the constant.

Grab (`_handGrab`, middle tip to palm) is out of scope. It keeps single-frame
cancel; when a grab is held during a dropout the whole hand cancels at once, as
before.

### Also in this change

- `_handTrackingEnabled` is `std::atomic<bool>`. The JNI thread writes it,
  the frame thread reads it once per frame.
- `initHandTracking`: when the right tracker fails to create, the left tracker
  is destroyed through `destroyHandTrackers()` and both handles end null. The
  failed right handle is never passed to `xrDestroyHandTrackerEXT`.

### Tests

`android/sharedCode/src/test/cpp/VROHandPinchStateTest.cpp` covers first
sample already pinched, re-arm after cancel, hysteresis on both metrics, the
exact 0.7499 / 0.5499 edges, inf / NaN / negative inputs, the grace window
(2 invalid frames hold, 3 cancel, cancel emitted once), immediate cancel on
system gesture, metric latch across a press, and two independent hands. It uses
a `CHECK` macro that always evaluates and exits non-zero, so it still tests
under `NDEBUG`.

Run locally with `tools/run-host-tests.sh` (plain `c++ -std=c++14 -Wall -Wextra
-Werror`, no Android toolchain) or through
`android/sharedCode/src/test/cpp/CMakeLists.txt` with `ctest`. The
`controller-ink.yml` workflow runs the script on every pull request. This is
the first host-test wiring in the repo; the bindings test was previously only
runnable by hand.

### Still open

Thresholds (0.75 / 0.55, 20 mm / 25 mm) and the 3-frame window are candidates
and have not been qualified on a device.
