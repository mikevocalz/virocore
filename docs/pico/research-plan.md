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
