# PICO first-run research script

Six questions for first-session observation with PICO users (Neo3, PICO 4,
PICO 4 Ultra). Run as a moderated first-run: hand the participant the headset
with the app installed, observe, then ask. Observation comes before the
question wherever possible so the answer doesn't contaminate the behavior.

## 1. Standing vs seated start

- **Ask:** "How do you usually start a headset session — standing or sitting?
  Did you change position during this one?"
- **Observe:** Whether the participant puts the headset on standing or seated,
  and whether they sit down (or stand up) within the first two minutes.
- **Feeds:** The default for the onboarding comfort check ("Standing" /
  "Seated") and whether `Floor` mode should be offered at all for seated users,
  whose eye height relative to the floor differs most from a standing start.

## 2. Where is the floor?

- **Ask:** With a floor-level object visible in Floor mode: "Point to where the
  app thinks the floor is. Does that match the real floor?"
- **Observe:** Whether they reach toward or step on floor-level content, and
  whether they hesitate or flinch when it doesn't align.
- **Feeds:** Acceptance threshold for the emulated floor offset (ADR-0001 rung
  2) — how much vertical error users notice and tolerate. The tolerance number
  itself: MEASURE ON DEVICE across participants.

## 3. Controller pick-up / put-down mid-session

- **Ask:** "Put the controllers down for a moment, then pick them back up. What
  did you expect the app to do?"
- **Observe:** How long before they notice input has switched, whether they
  look at their hands, whether they think the app broke.
- **Feeds:** Timing and wording of the "Controllers not detected — using hands"
  toast (`ux-copy.md`) and whether the hand-off should be silent when the swap
  is fast.

## 4. Discovering recenter

- **Ask:** "The content ended up behind you. How would you fix that?"
- **Observe:** Whether they try the PICO system recenter (long-press Home),
  look for an in-app button, or physically walk around. Do not hint.
- **Feeds:** Whether the in-app Recenter affordance needs an onboarding teach
  step, and whether the app must handle the system recenter path (the
  `REFERENCE_SPACE_CHANGE_PENDING` consequences in ADR-0001) as the primary
  path rather than a fallback.

## 5. Hand-tracking hand-off expectations

- **Ask:** "When you used your hands instead of controllers, what did you
  expect to be able to do? Anything you expected that didn't work?"
- **Observe:** Which gestures they attempt unprompted (pinch, poke, grab) and
  whether they expect parity with the controller feature set.
- **Feeds:** Which interactions must have hand equivalents before shipping
  hands-only support, and whether `hand_interaction` (PICO 4 Ultra only, per
  `device-profiles.md`) gates any of them on older devices.

## 6. Comfort with floor-level content

- **Ask:** "Some content sat at floor level. How did interacting with it feel —
  physically?"
- **Observe:** Whether they bend, crouch, or use a foot; whether they avoid
  floor content entirely; signs of imbalance while wearing the headset.
- **Feeds:** Whether Floor mode content should keep interactive elements above
  knee height by guideline, and the copy for any "look down" prompts. Interacts
  with question 1 — seated users cannot reach the floor at all.

---

# G5 — moderated usability pass

The six questions above are a first-run discovery script: they were written
before there was anything to fail. G5 is the other kind of study. The PICO/Quest
surfaces now exist in code, so G5 evaluates them against tasks, on hardware, with
a moderator present.

**Method: moderated usability testing.** Behavioural and attitudinal, qualitative,
scripted use, Design phase. Moderated rather than unmoderated for three reasons
specific to this product:

- A headset participant cannot fill in a form or read a prompt without leaving
  the experience, so the task has to be spoken and the observation has to be live.
- Several surfaces are silent by construction. `getCapabilities` collapses three
  distinct native rejections into one `null`
  (viro `components/Utilities/useOpenXRCapabilities.ts:29-33`), so the moderator
  has to know from the runbook which build is under test in order to read what
  the participant is reacting to.
- Exit is destructive and unconfirmed
  (viro `components/Studio/StudioQuestSceneHudOverlay.tsx:93-97`). An accidental
  exit ends the session; someone has to be there to restart it and record that it
  happened.

Five to eight participants per device tier. This produces a ranked list of
problems and their causes. It does not produce percentages — do not report
"40% of users failed T4" off six people. Benchmarking belongs after the defects
found here are fixed.

**Preconditions.** G5 cannot start until both blockers in
[the G5 runbook](https://github.com/mikevocalz/viro/blob/codex/pico-cli-bridge/docs/pico/g5-runbook.md)
are cleared. Running the tasks against a build that has either of them produces
findings about the harness rather than about the design.

## Tasks

Each task names the runbook case that establishes the build state it needs.
Run the runbook case first; if it fails, the task's result is void, not a finding.

| # | Said to the participant | Runbook cases | Success |
|---|---|---|---|
| T1 | "Start the experience." | K01 | Reaches the immersive scene from the 2D panel without being told which control does it. No prompting. |
| T2 | "The system is asking you something. Do whatever you'd normally do." | K02, K03 | Participant reads the request, makes a choice, and can afterwards say in their own words what they agreed to or refused. |
| T3 | "Can the app see your room? How can you tell?" | K04, K06 | Answers correctly from what is on screen, without the moderator explaining. Names the HUD status line as the source. |
| T4 | "Put an object on your table." | K05, K06 | Places an object, or states unprompted that surfaces are unavailable and uses manual placement instead. Either is a pass; silently failing to place anything is not. |
| T5 | "Something has gone wrong. Deal with it." (moderator triggers an alert) | K08 | Reads the alert, dismisses it, and can restate what it said. |
| T6 | "Which scene are you in? Now leave it." | K09, K10 | Reads the scene name off the HUD and exits deliberately on the first attempt. |
| T7 | "The content has ended up behind you. Fix it." | K11 | Reorients without physically walking, and without the moderator naming recenter. |
| T8 | "Capture a session log for this bug report." (developer participant, on the Mac) | K12 | Runs the CLI, hits a failure, and fixes it from the error text alone. |

T8 recruits a different population from T1–T7. The person who reads
`scripts/pico-cli.mjs` output is a developer on a laptop, not a headset user.
Recruit for it separately and do not average its results with the rest.

T4 is the task most likely to be void rather than failed. Plane detection is
predicted unreachable on every PICO tier
(`device-profiles.md:17-25`), so on PICO the K05 case is expected to record
"capability false, no anchors", and T4 becomes a test of the fallback copy
rather than of placement.

## Severity

Surface ids already occupy S1–S6 in `handoff.md` and `ux-copy.md`, so severity
bands are prefixed SEV to keep the two readable in the same sentence.

| Band | Meaning |
|---|---|
| SEV1 | The participant cannot complete the task and cannot recover inside the session. Data loss, a wedged scene, an exit they did not intend. |
| SEV2 | The participant completes the task only after the moderator intervenes, or completes it having formed a wrong belief about what the app did. |
| SEV3 | The participant completes the task unaided but visibly hesitates, backtracks, or reports discomfort. |
| SEV4 | Cosmetic or preference. The participant completes the task and raises the issue only when asked. |

Two modifiers, applied after the band is chosen. Each promotes by one band
(SEV4 → SEV3, and so on); both can apply to the same finding, and SEV1 is the
ceiling.

- **+1 if the failure is silent.** The app gave no signal that anything had gone
  wrong, so the participant had nothing to act on. This is the modifier that
  matters most on this product: the current build has at least three silent
  paths — the discarded permission result
  (viro `components/ViroXRSceneNavigator.tsx:268-271`), the three-way `null`
  from `getCapabilities`, and plane capability reported `true` while no anchors
  reach the scene
  (virocore `android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.cpp:619`
  versus `:1239-1241`).
- **+1 if it recurs after recovery.** The participant found a way through once
  and the same failure happened again on a later attempt. A problem you can
  learn your way out of is cheaper than one that keeps costing.

Record the base band and the modifiers separately, not just the total. A
SEV3 that became SEV1 through both modifiers needs a different fix from a
SEV1 that started there.

## Observer sheet

One page per participant per task. The observer is outside the headset watching
a cast. Fill it in during the run, not afterwards.

```
Participant ___   Device tier ___   Build (runbook header SHA) ___   Date ___
Task ___   Runbook cases run: ___   Cases passed: ___   Task void? Y / N

Start time ___   End time ___   Completed unaided?  Y / N / with help

What they did, in order (no interpretation):
  1. ______________________________________________
  2. ______________________________________________
  3. ______________________________________________
  4. ______________________________________________

First place they looked: ______________________________________
Where they expected the control to be: ________________________

Said out loud (verbatim, quotes only):
  ______________________________________________________________
  ______________________________________________________________

Moderator intervened?  N / Y — what was said: _________________

Physical signs:  bend  crouch  step back  reach and miss  remove headset
Reported discomfort:  none / eye strain / neck / balance / nausea

Silent failure?      Y / N   (nothing on screen changed when it should have)
Recurred after recovery?  Y / N   (same failure, later attempt)

Base band  SEV1  SEV2  SEV3  SEV4
Modifiers  [ ] silent  [ ] recurring        Final band ____

Surface(s) implicated (S1–S6): ________________________________
One-line problem statement:
  ______________________________________________________________
```

The "said out loud" box takes verbatim quotes only. Paraphrase in a headset
session is unreliable — the participant's face is covered and tone carries more
of the meaning than usual.

Record the build SHA from the runbook header on every sheet. Two G5 passes
against different builds are not comparable, and the capability-versus-anchor
split means two builds can look identical on screen while behaving differently
(`g5-runbook.md`, case K05).
