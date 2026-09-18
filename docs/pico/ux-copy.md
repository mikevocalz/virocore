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

---

# Final strings — S1 to S6

Copy for the surfaces specced in `handoff.md` under "As-built". Surface ids match.

Every error string states three things in order: what happened, why, and the one
action that changes the outcome. A string that names a failure without naming a
next action is not finished, however accurate it is.

Status of each string is marked:

- **shipped** — this exact text is in source at the cited line.
- **replaces** — a shipped string that should change, with the current text shown.
- **new** — no equivalent exists in code today.

File references without a prefix are
[viro @ `codex/pico-cli-bridge`](https://github.com/mikevocalz/viro/tree/codex/pico-cli-bridge).

## S1 — Permission request and denial

The request itself has no app-authored copy; Android renders the system dialog
from the three permission names at `components/ViroXRSceneNavigator.tsx:36-40`.
Everything below is **new**. The grant map is discarded at `:269-270`, so none of
these states can be reached until that result is read.

### Denied — anchor permission, Quest

> **Surfaces are off for this session**
> You declined room access, so the app can't find your floor, walls or table.
> Place objects with the controller, or allow room access in Settings →
> Apps → *App name* → Permissions and restart the scene.

Names the consequence before the remedy. A user who is happy placing by hand can
stop reading after the second line.

### Denied — headset camera, Quest

> **Object detection is off**
> You declined camera access, so the app can't recognise objects around you.
> Everything else works. Allow camera access in Settings → Apps →
> *App name* → Permissions to turn it back on.

"Everything else works" is the load-bearing sentence. Android will not re-prompt
after a denial, so without it the user has no way to know the session is still
usable.

### Granted but Space Setup missing, Quest

> **Finish Space Setup to use surfaces**
> Room access is allowed, but this headset has no saved room yet.
> Run Settings → Physical Space → Space Setup once, then come back.

A distinct state from denial, and the one most likely to be misread as a bug. The
permission is granted and plane detection still returns nothing, because Quest
plane data comes from the Space Setup room model rather than live scanning.

## S2 — In-scene alert

The alert renders whatever `questAlertStore.show(title, message)` was given
(`components/Studio/domain/questAlertStore.ts:31-35`); it authors no copy of its
own. Two rules for callers, plus one string this surface should own.

**Titles are sentence case, no terminal punctuation, five words or fewer.** At
`fontSize: 22` the title is a 0.22 m em box, 10.5° of arc at 1.2 m. A long title
wraps into a 1.8 × 0.3 m box and the panel does not grow.

**Every message ends with an action.** The panel is dismissed by clicking it and
nothing else happens, so a message that only describes a failure leaves the user
with a dismissed panel and an unchanged problem.

### Dismissal affordance — **new**

> Click anywhere to dismiss

There is no visible button. The click target is the whole 2 × 1 m panel
(`components/Studio/StudioQuestAlertOverlay.tsx:42`) with no rendered boundary
and no hover state, so the affordance has to be stated. Set as a third line at
caption size, below the message.

## S3 — Scene and exit HUD

### Scene name — **shipped**

> Untitled scene

`StudioQuestSceneHudOverlay.tsx:70`, the `sceneName ?? …` fallback. Keep.

### Plane status, unavailable — **shipped**

> Surface tracking unavailable — use manual placement

`:82`. Says what happened and what to do instead. Keep the text.

The bug is when it shows, not what it says: `planeDetectionAvailable` is false
while the capability is still `null`, so this line asserts a definite failure for
up to five seconds while the session is still resolving
(`components/Utilities/useOpenXRCapabilities.ts:26-27`). Gate it on
`planeDetection === false` and show the checking string below while it is `null`.

### Plane status, checking — **new**

> Checking for surfaces…

Fills the `null` window. Ellipsis matches the existing scanning string.

### Plane status, scanning — **shipped**

> Scanning for planes…

`:82`. "Planes" is renderer vocabulary; the unavailable line one row up already
says "surfaces". Prefer **Looking for surfaces…** and use one noun across the
three states.

### Plane status, found — **shipped**

> Plane found

`:82`. Same noun problem, and it is a bare state with no consequence. Prefer
**Surface found — tap to place**, which tells the user what the state bought them.

### Exit — **replaces**

Current: `[ Exit ]` (`:94`).

> Exit scene

The brackets are a text-mode convention standing in for a button that was never
drawn. They do not make the target discoverable and they do not bound it: the hit
area is the full 1.5 m text width, 61.3° of arc (`:93-97`). Give the row a
rendered background at the strip's real bounds and drop the brackets. "Exit
scene" also distinguishes the action from quitting the app, which the Meta button
does.

### Exit confirmation — **new**

> **Leave this scene?**
> Unsaved placements will be lost.
> [ Stay ]   [ Leave ]

Exit is destructive, unconfirmed, and one stray controller ray wide. Either
confirm it or make the target smaller than 61°; shipping both the wide target and
no confirmation is the part that cannot stand.

## S4 — Missing-plane fallback

The visible string is S3's status line. What is missing is copy for the case
where the capability call fails outright, which today is rendered identically to
"still initialising".

### Rebuild required — **new**

> **This build can't report surface support**
> The app's native renderer is older than the JavaScript it's running.
> Rebuild ViroCore and the Viro bridge together, then reinstall.

The native module already knows this precisely: it rejects with
`E_XR_REBUILD_REQUIRED` and the message "Rebuild both ViroCore and the Viro
bridge"
(`android/viro_bridge/src/main/java/com/viromedia/bridge/module/VRModuleOpenXR.java:75`),
raised from a `LinkageError`. The JS hook throws that away and returns
`{ planeDetection: null }` for every rejection
(`components/Utilities/useOpenXRCapabilities.ts:29-33`). Preserve the code across
the boundary and this string can be shown instead of a five-second lie. It is a
developer-facing state, so it belongs behind the `debug` flag rather than in a
user build.

### Capability true, no anchors — **new**

> **Surfaces are detected but not reaching this scene**
> This scene's root doesn't accept anchors on this platform.
> Placements will stay where you put them.

Only reachable on Quest, and only because capability and anchor delivery are
decoupled: `_planeDetectionStatus` is set at session creation
(virocore `android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.cpp:619`) while
anchors require a `VROARScene` root past the early return at `:1239-1241`.
Upstream merge [`5bddce6`](https://github.com/mikevocalz/viro/commit/5bddce6)
kept the Quest root at `ViroScene` and states the cost outright. Until the roots
are unified, this string is the only thing that tells a user why "Scanning for
planes…" never resolves.

## S5 — 2D launch panel

Every string here is **new**. The panel renders an empty `View` on a headset
(`components/ViroXRSceneNavigator.tsx:323`).

### Scene is running in the headset

> **Running in the headset**
> Put the headset on to continue. Press B to come back here.

Covers the whole time VRActivity owns the display. Without it the panel is blank
and gives no reason.

### Launching

> Starting the scene…

Shown from the launch effect until VRActivity takes the display. On Quest this
window includes the S1 permission dialog, which is awaited
(`:269`) before `launchVRScene()` (`:280`).

### Returned from the headset, host did not navigate

> **You've left the scene**
> Nothing is running in the headset now.
> Choose a scene to start again.

`onExitViro` is the host app's responsibility
(`components/Studio/StudioSceneNavigator.tsx:582`). If the host does not navigate
on exit, the user lands on a blank panel with no explanation. This is the string
for that, not a substitute for wiring `onExitViro`.

### Launcher missing

> **This build can't start XR**
> The native XR launcher isn't in this app.
> Rebuild the development client with the XR plugin enabled.

Replaces the raw throw at `:261`, whose text —
"[Viro] OpenXR launcher is missing. Rebuild the native development client." — is
close, but reaches the user through `reportQuestError` rather than as panel copy.

## S6 — CLI and prebuild errors

expo-pico @ `codex/pico-cli-bridge`. These are read on a laptop, so they may name
files, flags and commands directly.

### `scripts/pico-cli.mjs` — **shipped, keep**

| Line | Text |
|---|---|
| `:31` | `Invalid option: <key>` + usage |
| `:33` | `Duplicate option: <key>` |
| `:37` | `<command> requires <key>` |
| `:46` | `Unknown command: <command>` |
| `:50` | `--apk must be an APK` |
| `:51` | `APK not found: <path>` |
| `:56` | `Invalid Android package ID` |
| `:101` | `PICO CLI exited <status>` |
| `:131` | `On Windows run this script through npm run pico:<command>.` |

`:31` is the model for the rest: it names the bad input and prints the usage
block that contains the fix. `:131` names the exact replacement command.

### `--device` has no validation — **new**

> `--device must be a device serial (letters, digits, dot, dash, underscore)`

`--apk` is checked for an extension (`:50`) and `--package` against a regex
(`:55`), but `--device` takes whatever follows it. The only filter is
`args[i + 1].startsWith('--')` (`:29`), which a single-dash value passes. See
`code-review.md` M8.

### `Invalid Android package ID` — **replaces**

> `--package must be an Android package ID, e.g. com.example.app (got "<value>")`

`:56` names the rule but not the input, the flag, or the shape. The regex at `:55`
is already the spec; the message should say what it enforces.

### `PICO CLI exited <status>` — **replaces**

> `PICO CLI exited <status> running "<args>". Its output is above; run pico:doctor to check the toolchain.`

`:101` gives a number with no context. `validateResult` has the arg list in hand
and stderr has already been written through (`:142`), so pointing at both costs
nothing.

### `withPicoOpenXrLoaderOverlay.ts` — **shipped, keep**

`:41-43`:

> `[expo-pico-core] Missing staged <path>. Disable the overlay to use a rebuilt AAR.`

What happened, which file, and the fix.

### `Invalid overlay state path` — **replaces**

> `[expo-pico-core] Overlay state names a path outside the project: <relative>. Delete the stale overlay state file and re-run prebuild.`

`:74`. The current string says nothing about which path, why it is invalid, or
what to do. The check is a containment test against `sourceRoot` (`:73`), so the
offending value is available.

### Custom override review — **replaces**

The message contradicts its own condition. It fires when
`current !== previous[relative] && current !== known.get(relative)` (`:78`) —
the file **was** modified — and then says "it was not modified" (`:79-81`).

> `[expo-pico-core] <path> was modified outside prebuild. Review the change, then either revert it or delete the file to let prebuild regenerate it.`

### Prebuild preconditions — **new**

Two failures currently reach a person as a blank scene rather than as text. Both
are checkable at prebuild time and both block G5 (see
`research-plan.md`, Preconditions).

> `[expo-pico-core] index.js calls registerImmersiveScene, which overwrites Viro's VRQuestScene registration and removes ViroQuestEntryPoint. Mount ViroXRSceneNavigator from the panel instead, or subscribe your custom root to VRQuestNavigatorBridge. See docs/VIRO-PICO-INTEGRATION.md:35.`

> `[expo-pico-core] @reactvision/react-viro <version> is the public release and has no getCapabilities. Surface detection will report "unavailable" on every device. Install the fork build from docs/VIRO-PICO-INTEGRATION.md before running on hardware.`
