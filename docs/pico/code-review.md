# Code review — coordinated PICO integration

Findings across the three repositories on `codex/pico-cli-bridge`. `INTEGRATION.md`
describes what the change does; this is what is wrong with it.

Repositories, abbreviated below:

- **virocore** — this repo. Renderer and JNI.
- **viro** — [mikevocalz/viro](https://github.com/mikevocalz/viro/tree/codex/pico-cli-bridge).
  JS components, Android bridge, prebuilt binaries.
- **expo-pico** — [mikevocalz/expo-pico](https://github.com/mikevocalz/expo-pico/tree/codex/pico-cli-bridge).
  Config plugin, Gradle generation, CLI.

Status on each finding:

- **open** — present at HEAD, no fix in progress.
- **fix in flight** — being addressed in a parallel change; recorded so it is not
  re-reported.
- **fixed** — resolved on this branch; kept because the shape of the mistake is
  worth not repeating.

Design and copy findings are not here. They are in `handoff.md`, `ux-copy.md`
and viro's `docs/pico/design-system-audit.md`.

---

## Blockers

### B1 — `matchingFallbacks` emitted on `defaultConfig` — *fix in flight*

expo-pico `packages/expo-pico-core/plugin/src/withPicoGradle.ts:344-356`

The generated `subprojects` block reaches into every autolinked
`com.android.library` and writes into its `defaultConfig`. `matchingFallbacks` is
declared on AGP's `ProductFlavor`, not on `DefaultConfig`/`BaseFlavor`. Emitting
it there fails at configuration time, and because the block is applied to every
Android library in the build, it fails the whole build rather than one module.

The correct property for a dimension a library does not declare is
`missingDimensionStrategy`, which is what the block now emits (`:351`). The
flavor-level uses at `:86`, `:101` and `:108` are on real `ProductFlavor`s and are
right.

A regression test pins both halves —
`packages/expo-pico-core/__tests__/picoViroIntegration.test.ts:106-115` asserts
the subprojects block contains `missingDimensionStrategy 'device', 'mobile'` and
does **not** contain `matchingFallbacks`.

The distinction is also written down in expo-pico
`docs/VIRO-PICO-INTEGRATION.md:33`, and the comment at
`withPicoGradle.ts:92-93` states it inline. Both predate the fix.

### B2 — JNI handle dereferenced with no validity check

virocore `android/sharedCode/src/main/cpp/jni/VROVirtualController_JNI.cpp:36-42` — *open*

```cpp
static inline jlong toRef(std::shared_ptr<VROInputState> state) {
    return reinterpret_cast<intptr_t>(new PersistentRef<VROInputState>(state));
}

static inline std::shared_ptr<VROInputState> fromRef(jlong ref) {
    return reinterpret_cast<PersistentRef<VROInputState> *>(ref)->get();
}
```

`fromRef` casts an arbitrary `jlong` that arrived from Java and immediately
dereferences it. There is no null check and no validity check. Five entry points
route through it: `nativeSetStickL` (`:58`), `nativeSetStickR` (`:62`),
`nativeSetButton` (`:66`), and the button view's `nativeSetButton` (`:85`).

`toRef` (`:36-38`) hands Java a raw heap pointer as a `jlong`, and Java stores it
in a plain, non-`volatile` `long` field:
viro `android/viro_bridge/src/main/java/com/viromedia/bridge/component/VRTVirtualJoystickView.java:66`
and `VRTVirtualButtonView.java:81`.

The only defence is the caller's discipline, and it is one comparison:
`if (mNativeRef == 0) return;` at `VRTVirtualJoystickView.java:214` and
`VRTVirtualButtonView.java:206`. That guard does not cover the teardown window.
`releaseRegistry()` calls `nativeRelease` — which `delete`s the `PersistentRef`
at `VROVirtualController_JNI.cpp:55` and `:82` — and only then clears the field
at `VRTVirtualJoystickView.java:127` / `VRTVirtualButtonView.java:146`. Between
the `delete` and the field write, `mNativeRef` holds a freed pointer that
`fromRef` will dereference without complaint.

**What is not established.** No second-thread caller of `writeStick` or
`setButton` was found in this tree; RN prop updates and touch dispatch both run
on the UI thread, which would serialise the window. So this is an unguarded
boundary with a reachable-looking race, not a demonstrated crash. It should be
closed on the strength of the boundary alone: a JNI entry point that trusts a
`jlong` is one Java-side refactor away from a native fault with no stack.

**Fix.** Null-check in `fromRef` and return an empty `shared_ptr`; make every
call site handle it. Mark `mNativeRef` `volatile` on both Java classes. Clear the
Java field before calling `nativeRelease`, not after.

### B3 — `ios/dist` framework forked mid-bundle — *fixed*

viro `ios/dist/ViroRenderer/ViroKit.framework/`

At [`c09d674`](https://github.com/mikevocalz/viro/commit/c09d674), the commit that
integrated Viro #526–528, the diff against `origin/develop` under `ios/dist` was
three files: the `ViroKit` Mach-O binary, `include/VRTColocationModule.h`, and
`lib/libViroReact.a`. The framework's `Info.plist`, `Shaders.dat`,
`default.metallib` and `Headers/` stayed at develop's content.

A framework whose binary is rebuilt from fork source while its shader archive,
headers and bundle metadata come from a different tree is not a coherent
artifact. `Shaders.dat` in particular is consumed by that binary at runtime, and
nothing checks that the two were produced together.

Resolved by [`8afed41`](https://github.com/mikevocalz/viro/commit/8afed41),
"Rebuild the Android and iOS binaries from fork source", which moved
`Info.plist` (812 → 813 bytes), `Shaders.dat`, `default.metallib`,
`Headers/VROImage.h` and `Headers/VRORenderer.h` along with the binary. The
current diff against `origin/develop` covers eleven files rather than three.

**Keep the lesson.** Binaries in source control fork silently. Stage a framework
as a unit or not at all; a partial stage produces a tree that builds and a
runtime that does not match its headers.

---

## Majors

### M3 — duplicate ZIP entries bypass alignment verification — *fix in flight*

expo-pico `scripts/verify-16kb-alignment.py:74-80`

```python
with zipfile.ZipFile(artifact) as archive:
    entries = [n for n in archive.namelist() if n.endswith(".so") and ...]
    ...
    for name in sorted(entries):
        check(f"{artifact}!{name}", archive.read(name))
```

`namelist()` returns one string per central-directory record, including
duplicates. `read(name)` resolves through `NameToInfo`, which keeps the **last**
record for a repeated name. A ZIP carrying `lib/arm64-v8a/libfoo.so` twice
therefore yields two identical checks of the second copy, and the first copy is
never read.

The verifier then reports `OK` for an archive containing a member it did not
inspect. Whether the Android packager extracts the first or the last record is
not something this script should be betting on.

**Fix.** Iterate `archive.infolist()` and read through `archive.open(info)`, which
addresses records rather than names. A duplicate name then becomes two distinct
checks of two distinct members, and a name appearing more than once is worth
failing on outright.

### M8 — `--device` is unvalidated, and the option parser accepts single-dash values

expo-pico `scripts/pico-cli.mjs:26-34, 47, 52, 57, 61, 68-72, 83-85` — *open*

Two defects that compound.

**The parser's guard is a prefix test.** An option value is rejected only if it
starts with `--`:

```js
if (
  !['--device', '--apk', '--package', '--out'].includes(key) ||
  !args[i + 1] ||
  args[i + 1].startsWith('--')
) {
  throw new Error(`Invalid option: ${key}\n${usage}`);
}
```

`:29`. A single-dash value passes. Calling `plan(['launch', '--device', '-s',
'--package', 'com.example.app'])` returns
`["app", "launch", "com.example.app", "--device", "-s"]` — `-s` was accepted as
the serial and forwarded. `--device --evil` is rejected; `--device -evil` is not.

**`--device` has no format check at all.** `--apk` must end in `.apk` (`:50`) and
must exist on disk (`:51`); `--package` is matched against
`/^[A-Za-z_]\w*(\.[A-Za-z_]\w*)+$/` (`:55`). `--device` is passed through
`requireValue`, which only trims and checks for emptiness (`:36-39`), and then
injected into the downstream argv at `:52`, `:57`, `:61`, `:68-72` and `:83-85`.

**The sink is the PICO CLI's own argv, not a shell.** `spawnSync` is called with
`shell: false` (`:134`) and the `npx` entry point is resolved through
`process.execPath` where possible (`:125-132`), so there is no shell
interpolation on any platform — the Windows branch throws rather than falling
back (`:130-131`). The injected value also arrives downstream as a single argv
element: `--device '-x --format yaml'` is forwarded as one token, not split into
two flags. So the reachable capability is one attacker-chosen argv token
beginning with `-`, interpreted by `@picoxr/pico-cli`'s own option parser, whose
surface is not audited here. That is argument injection, bounded to one token. It
is not command injection, and calling it that would misdirect the fix.

**`--out` escapes the working tree.** `path.resolve(requireValue('--out'))` at
`:59` with no containment check, then `mkdirSync(..., { recursive: true })` at
`:123` and `writeFileSync` at `:139`. Planning
`capture --out ../../escaped` resolves the three output files to
`<two levels above cwd>/escaped/device.json`, `logcat.json` and `screenshot.png`,
and the `mkdirSync` creates the directory to hold them. The same single-dash hole
applies to this flag.

**Fix.** Reject any value beginning with `-`, not just `--`. Validate `--device`
against the serial charset the PICO CLI accepts. Resolve `--out` and require the
result to sit under the project root, the same containment test
`withPicoOpenXrLoaderOverlay.ts:73` already performs for overlay paths.

### M15 — PPS dependencies are declared unscoped

expo-pico `packages/expo-pico-core/plugin/src/ppsArtifacts.ts:192-198`,
`withPicoGradle.ts:214-219` — *open*

`renderPpsDependenciesBlock` emits bare `implementation` lines:

```js
`    implementation "${PPS_GROUP}:platform-service-${svc}:${version}"`
```

The block is appended to the app module's `build.gradle`
(`withPicoGradle.ts:219`) gated only on `options.xrMode !== 'mobile'`
(`:214`). `implementation` with no flavor prefix applies to every variant of the
module, so a `dual` configuration — or a `pico` configuration that also declares
the `quest` flavor (`withPicoGradle.ts:94-103`) — pulls the PICO Platform Service
SDK into the `mobile` and `quest` variants as well.

The `constraints` block above it (`:191-196`, `:213-215`) pins the whole service
set including services the app does not use, which is deliberate and documented
at `:208-211`. That is a resolution pin and carries no artifact. The
`implementation` lines carry artifacts, and they are not scoped.

The project already knows the scoped form:
`packages/expo-pico-app-kit/README.md:98` and `:107` tell consumers to use
`picoImplementation`. The generator does not.

**Fix.** Emit `picoImplementation` / `dualImplementation` to match the flavors
`renderFlavorBlock` actually created for this `buildVariant`, and leave the
`constraints` block where it is.

### M — prebuild is not a fixpoint until the third run

expo-pico `packages/expo-pico-core/plugin/src/withPicoGradle.ts:126-133, 153` — *open*

Applying `withPicoAppBuildGradle` repeatedly to a minimal `app/build.gradle`
(`xrMode: 'pico-os5'`, `buildVariant: 'dual'`):

```
lengths base/r1/r2/r3/r4: 299 6870 6870 6870 6870
r1===r2: false | r2===r3: true | r3===r4: true
```

Run 1 and run 2 are the same length and different content. The `// expo-pico-core:
begin flavor overlays` block lands mid-file on the first run and at the tail on
every run after, because `updateOverlayPackaging` strips it with a regex
(`:131-134`) and the caller re-appends it. From run 2 on the output is stable.

Both placements are top-level `androidComponents` blocks, so the two files are
semantically equivalent and nothing breaks. What breaks is anyone diffing a fresh
prebuild against a committed one: the block appears to move, once, for no reason
visible in the diff.

The repo's own idempotency coverage does not reach this. Every test compares run
1 against run 2, and the one test that names `withPicoAppBuildGradle` says so
outright — `packages/expo-pico-core/__tests__/withPicoGradle.test.ts:252-260`,
"we can only smoke test the factory contract here".

**Fix.** Append the overlay block at a stable anchor rather than at the end, or
strip and re-append unconditionally on every run so run 1 and run 2 agree. Extend
the test to three applications and assert `r2 === r3`.

---

## Where the rest lives

Behavioural findings about the renderer and the JS surfaces are recorded where
the surfaces are specced rather than duplicated here:

- Discarded permission grant map, and why it is functional rather than cosmetic —
  `handoff.md` S1, `ux-copy.md` S1, viro `docs/pico/g5-runbook.md` K03.
- Three native rejection codes collapsed into one `null` — `handoff.md` S4,
  runbook K04.
- Plane capability reported independently of anchor delivery — `handoff.md` S4,
  runbook K05.
- Unbounded, unconfirmed exit target — `handoff.md` S3, runbook K09.
- Hardcoded values and the absent token module — viro
  `docs/pico/design-system-audit.md`.
