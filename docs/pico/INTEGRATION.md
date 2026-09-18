# Coordinated Viro PICO source integration

This branch keeps PICO inside Viro's native OpenXR renderer, sharing session and
scene infrastructure with Quest. Expo-PICO supplies PICO Android flavors and
launcher metadata; it does not substitute for the renderer. See the coordinated
[Expo-PICO integration guide](https://github.com/mikevocalz/expo-pico/blob/codex/pico-cli-bridge/docs/VIRO-PICO-INTEGRATION.md).

The source integration includes ReactVision/virocore
[#376](https://github.com/ReactVision/virocore/pull/376) for Viro #527 web parity and
the Podfile updates from [#377](https://github.com/ReactVision/virocore/pull/377)
for Viro #528. Existing custom native archives are preserved; CCA and ViroKit
must be rebuilt together rather than replaced with upstream binaries that may
omit the fork's Moyo changes.

The new plane capability getter reports -1 until session initialization, 0 if no
plane source initializes, and 1 if one initializes. The atomic field can be read
from the Java UI thread without changing Expo-PICO's existing String[10] runtime
probe contract. This is initialization state, not a guarantee of available planes
or an eventual successful asynchronous scan.

Build with `./scripts/build-pico-aar.sh --out /absolute/artifacts`. The NDK pin is
read from the actual `android/viroreact` module, its r27 flexible-page-size mode is
enabled, and the ELF verifier rejects empty or corrupt artifacts. The resulting
JSON records source commit, dirty state and artifact SHA-256. Rebuild the Viro
Java bridge against this AAR before app packaging. Validate APK ZIP alignment
separately and run the guide's hardware checks.

Local validation covered parser fixtures, the staged Expo-PICO loader and renderer
ELF headers, and source review. Android, WASM, iOS and visionOS builds require their
native toolchains and have not been run in the integration workspace. In particular,
reconcile Viro's imported 15.1 podspec declaration with the renderer Xcode project's
17.6 targets when rebuilding the iOS binary set.

## Design and QA documents

Alongside this file: `research-plan.md` (first-run script and the G5 moderated
usability pass), `handoff.md` (as-built spec for surfaces S1–S5),
`ux-copy.md` (final strings S1–S6), `code-review.md` (blockers and majors across
all three repositories), plus `device-profiles.md`,
`adr-0001-reference-space.md` and `mobbin-refs.md`.

Two documents live in
[Viro's `docs/pico/`](https://github.com/mikevocalz/viro/tree/codex/pico-cli-bridge/docs/pico)
rather than here, because they describe components in that repository:

- `g5-runbook.md` — the on-device gate, cases K01–K12. `research-plan.md` names
  these cases; `StudioSceneNavigator.tsx:577` defers to K07 for the PICO HDR
  decision.
- `design-system-audit.md` — hardcoded values in the in-scene HUD and alert,
  measured against the spatial layout tokens.
