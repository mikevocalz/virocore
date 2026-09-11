# UX copy — PICO reference-space and input work

Four user-facing strings introduced by this work. All are plain language; none
use OpenXR terms. Placement distances reference the `comfortableUI` token from
`viroreact-spatial-layout-system` (1.5 m), per the comfort skill.

## "Recenter"

- **Where:** Button label on the home/root panel, always reachable. The comfort
  skill requires this exact label — not "Reset position", not icon-only.
- **Trigger:** Always visible on the root panel. Additionally offered as a
  non-modal "Recenter?" toast when the app resumes from background and the head
  pose has rotated more than 45 degrees or moved more than 1 m since the last
  known position (comfort skill §4).
- **Dismissal:** The button is persistent. The resume toast auto-dismisses
  after a timeout or on any other interaction.
- **Decline/ignore path:** Ignoring it costs nothing — content stays where it
  was. The user can also use PICO's system recenter (long-press Home) instead;
  the app handles that path too (ADR-0001).

## "Controllers not detected — using hands"

- **Where:** Status toast at `comfortableUI` distance, low in the view so it
  does not cover content.
- **Trigger:** The input controller loses controller tracking and hand tracking
  takes over (put-down, battery death, out of range).
- **Dismissal:** Auto-dismisses after a few seconds, or immediately when
  controllers come back. Never re-shown for the same continuous hands session.
- **Decline/ignore path:** Purely informational — no action required. Ignoring
  it changes nothing; hands already work. Research question 3 in
  `research-plan.md` decides whether fast swaps should skip the toast entirely.

## "Floor level set"

- **Where:** Brief confirmation toast at `comfortableUI` distance.
- **Trigger:** Floor mode resolves successfully (either ladder rung 1 or 2 in
  ADR-0001) at session start, or after a recenter rebuilds the floor space.
- **Dismissal:** Auto-dismisses after a few seconds. No button.
- **Decline/ignore path:** Informational only. If the floor could not be set
  (ladder rung 3), this toast never appears; the app receives the actual mode
  and adapts silently — the failure is not the user's problem to solve.

## "Look around to reorient"

- **Where:** Soft hint on a 60%-opacity card, head-locked, low in the view —
  the tracking-loss overlay placement from the comfort skill §7.2.
- **Trigger:** Tracking enters the limited/relocalizing state. Wording follows
  the comfort skill's relocalizing hint; it tells the user the one action that
  actually helps.
- **Dismissal:** Auto-dismisses when tracking returns to normal. Announced once
  per state change, never repeated.
- **Decline/ignore path:** The user can ignore it and wait; world-locked
  content stays put and the rest of the UI (recenter, settings, exit) stays
  interactive. Placement actions are suppressed until tracking recovers, so
  ignoring the hint only prolongs the limited state.
