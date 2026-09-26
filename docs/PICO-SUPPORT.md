# PICO support — mikevocalz/virocore fork

This fork adds PICO OS 5.9+ / OS 6 / Swan support to virocore's OpenXR backend.
It tracks upstream `ReactVision/virocore` and is meant to be **upstreamable** —
every change is vendor-neutral OpenXR with no PICO `#ifdef`s, so the long-term
goal is to merge it back and retire the fork.

## Status — 2026-09-26 (supersedes "Layout" and "Build boundary" below)

- The work branch is now `upgrade/virocore-3.0.1`: upstream tag `v3.0.1`
  merged (`e7bef021`), then `codex/rive-viro-panel` and `fork/main` were
  merged in. It landed on `mikevocalz/virocore` `main` at **`d4e09840`**
  (`git describe` = `v3.0.1-123-gd4e09840`), pushed with tags; upstream
  GitHub releases mirrored 23/25 (two malformed `rc-*` refs rejected),
  `Latest` = v3.0.1.
- The AAR **has** since been built repeatedly from integrated source —
  `:viroreact:assembleRelease` under JDK 17 — and the 16 KB gate passes all
  14 arm64 libraries including vendored `libopenxr_loader.so`. The "build was
  NOT executed" note under *Build boundary* describes the original commits'
  authoring environment, not the current state.
- Consumption did not go the renamed-package route: the rebuilt
  `viro_renderer-release.aar` ships **inside** `@reactvision/react-viro`
  version `3.0.1-moyo.0`, vendored as a tarball in Moyo
  (`vendors/reactvision-react-viro-3.0.1-moyo.0.tgz`). Renderer and bridge
  AARs are built from the same merge and must stay version-paired — bump them
  together.
- Still open: all PICO on-device items (Neo3 / PICO 4 / PICO 4 Ultra checks
  in the *Build boundary* list and `docs/pico/handoff.md`).

## Layout (original fork layout, pre-3.0.1)

- Base: upstream tag `v2.56.0` (commit `5c3d101`).
- Work branch: `pico-support`.
- Two native commits:
  1. input — PICO interaction profiles.
  2. renderer — runtime detection, soft-required extensions, foveation, JNI.
- `scripts/build-pico-aar.sh` — builds `viro_renderer-release.aar`.
- `.github/workflows/build-pico-aar.yml` — CI builds the AAR per push/tag.

## How it's consumed

The JS package `@reactvision/react-viro` ships a **prebuilt**
`android/viro_renderer/viro_renderer-release.aar` and re-exports it via gradle.
So consumption is just: build the AAR here → drop it into the JS-package fork
(`mikevocalz/viro`) → republish as `@mikevocalz/react-viro`. `expo-pico` depends
on that renamed package instead of patching `node_modules`.

```
mikevocalz/virocore (native)  --build-pico-aar.sh-->  viro_renderer-release.aar
        │                                                      │
        └── CI artifact ──────────────────────────────────────┘
                                                               ▼
mikevocalz/viro (JS fork)  --bundles AAR, renames pkg-->  @mikevocalz/react-viro
                                                               ▼
expo-pico  --depends on @mikevocalz/react-viro-->  app builds with PICO support
```

## Staying in sync with upstream

Keep the fork a thin, rebasable delta:

```bash
# one-time
git remote add upstream https://github.com/ReactVision/virocore.git

# when upstream tags a new release (e.g. v2.57.0)
git fetch upstream --tags
git checkout -b pico-support-2.57.0 v2.57.0
git cherry-pick <input-commit> <renderer-commit>   # the two pico commits
# resolve any conflicts (most likely in the extension arrays or swapchain block)
./scripts/build-pico-aar.sh                          # rebuild + sanity-grep
```

Because the two commits are small and touch a contained region (OpenXR backend
only), rebasing onto a new upstream tag is usually a clean cherry-pick. If a
conflict appears it'll be in `VROSceneRendererOpenXR.cpp`'s extension list or
swapchain creation — both well-localized.

## Upstreaming

File the issue in `ISSUE-pico-openxr-support.md` first. If ReactVision wants it,
these same two commits become the PR (rendering+input+foveation), with anchors /
scene mesh / body+eye tracking / MRC as separate follow-up PRs per
`PICO-FEATURE-ROADMAP.md`. Once merged upstream, retire this fork and point
`expo-pico` back at `@reactvision/react-viro`.

## 16KB page alignment — Android 15 / SDK 35 (ReactVision/viro#485)

Android 15 requires every 64-bit (`arm64-v8a`) `.so` to be 16KB page-aligned
(ELF `PT_LOAD` `p_align >= 0x4000`). Upstream `@reactvision/react-viro@2.56.0`
shipped a prebuilt AAR whose `libopenxr_loader.so` and `libc++_shared.so` were
still 4KB-aligned (`0x1000`), causing Play Console `.aab` rejection. You can't
work around it by stripping the loader — `libviro_renderer.so` hard-links OpenXR
symbols, so removing it crashes at launch with `UnsatisfiedLinkError`.

**Why the fork fixes it automatically.** Since the rebase onto upstream
`develop` (post-2.58.1), upstream's own build config already handles alignment:
- NDK pinned to **r27.1** (`27.1.12297006`) tree-wide — r27 is the first NDK
  whose `libc++_shared.so` is 16KB-aligned.
- `-Wl,-z,max-page-size=16384` linker flag (CMakeLists) — makes
  `libviro_renderer.so` and every lib we compile 16KB-aligned.
- OpenXR loader at **1.1.62**, vendored in `android/openxr_sdk/maven/` — its
  arm64-v8a `libopenxr_loader.so` reports `PT_LOAD` align `0x4000` (verified by
  parsing the vendored AAR's ELF headers).

Because the fork **rebuilds** the AAR from this source, the output is
16KB-aligned. We don't inherit the stale binary.

**The gate that guarantees it.** `scripts/verify-16kb-alignment.py` parses every
arm64-v8a `.so` in the built AAR and fails the build if any `PT_LOAD` segment is
< `0x4000`. It runs inside `build-pico-aar.sh` and again as a CI step, so we
provably never publish a misaligned AAR the way upstream did. Verified to flag
the exact two libs from #485 on the upstream artifact.

**Loader is vendored.** The local Maven mirror under
`android/openxr_sdk/maven/` carries 1.1.62, which the build resolves offline —
no population step needed. To bump the loader later, mirror the new version
into that directory from `repo1.maven.org` and update the pin in
`android/viroreact/build.gradle`, then re-run the alignment gate.

**Consumer side.** In the app, set `android.packagingOptions.jniLibs.useLegacyPackaging = false`
(the default on AGP 8+ / `compileSdk 35`) so libs are page-aligned and loaded
uncompressed from the APK. The fork's AAR provides aligned binaries; legacy
packaging would re-introduce misalignment.



The feature-work items (spatial anchors, room/scene mesh, body/eye/motion
tracking, human occlusion, MRC) are NOT here — they need new scene-graph
subsystems + JS component APIs, not OpenXR flags. See `PICO-FEATURE-ROADMAP.md`.
This fork is "renders + full controller/hand input + foveation on PICO," which
is the bar for most apps.

## Build boundary (honest note)

The AAR build (`./gradlew :viroreact:assembleRelease` — `:sharedCode` is
IDE-only and produces no AAR) requires the Android
NDK toolchain and was NOT executed in the environment that authored these
commits — the source changes are complete and self-consistent, but the compiled
`.so` / `.aar` and on-device validation are your step. The CI workflow exists to
make that build reproducible rather than machine-dependent. Before tagging a
release, also verify on your PICO 4 Ultra (B3110):
- the FB foveation enum spellings against your NDK's openxr headers,
- the PICO OS version system-property key,
- controllers respond (the input fix), and
- `getRuntimeInfo()` returns `vendor=PICO`.
