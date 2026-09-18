#!/usr/bin/env bash
#
# build-pico-aar.sh — build the patched virocore renderer AAR and stage it for
# the @mikevocalz/react-viro JS package.
#
# This is the bridge between the two forks:
#   mikevocalz/virocore  (this repo, native)  -- produces -->  viro_renderer-release.aar
#   mikevocalz/viro      (JS package fork)      -- consumes --> bundles the AAR, renames pkg
#
# Prereqs (NOT available in CI-less envs — this is the real toolchain boundary):
#   - Android SDK + NDK (matching virocore's build.gradle ndkVersion)
#   - JDK 17, Gradle wrapper present in android/
#   - ANDROID_HOME / ANDROID_NDK_HOME exported
#
# Usage:
#   ./scripts/build-pico-aar.sh [--out <dir>]
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$REPO_ROOT/dist-aar"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) [[ $# -ge 2 ]] || { echo "--out requires a directory"; exit 2; }; OUT_DIR="$2"; shift 2;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd -- "$OUT_DIR" && pwd)"
cd "$REPO_ROOT"

echo "==> Building patched virocore renderer AAR (PICO support)"
echo "    branch: $(git rev-parse --abbrev-ref HEAD)  commit: $(git rev-parse --short HEAD)"

# Sanity: confirm the PICO patches are actually present in this checkout, so we
# never publish an AAR that silently lacks the work.
grep -q "bytedance/pico4s_controller" \
  android/sharedCode/src/main/cpp/VROInputControllerOpenXR.cpp \
  || { echo "ERROR: PICO interaction profiles missing — wrong branch?"; exit 1; }
grep -q "VROOpenXRRuntimeInfo" \
  android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.h \
  || { echo "ERROR: runtime-info struct missing — wrong branch?"; exit 1; }

cd android
# :viroreact is the module with externalNativeBuild wiring for viro_renderer.
# :sharedCode is IDE-only (see android/sharedCode/build.gradle:5) and produces
# an AAR without the native libs we need.
echo "==> ./gradlew :viroreact:assembleRelease"
./gradlew :viroreact:assembleRelease

AAR_PATH="viroreact/build/outputs/aar/viroreact-release.aar"
[[ -f "$AAR_PATH" ]] || { echo "ERROR: AAR not produced at $AAR_PATH"; exit 1; }

# ── 16KB page-alignment gate (ReactVision/viro#485) ───────────────────────────
# Verify ELF load alignment for devices using 16KB pages. OS version is not
# evidence of page size. APK ZIP alignment must also be verified after app packaging.
echo "==> Verifying 16KB page alignment of arm64-v8a libraries"
cd ..  # back to repo root for the verifier
# Keep the verifier's output: the alignment it measured is what the provenance
# record reports, rather than a constant asserting what it ought to have been.
if ! ALIGN_REPORT="$(python3 scripts/verify-16kb-alignment.py "android/$AAR_PATH")"; then
    printf '%s\n' "$ALIGN_REPORT"
    echo "ERROR: AAR contains 4KB-aligned arm64-v8a libraries — refusing to stage."
    echo "       Check NDK version (need r27+), the -Wl,-z,max-page-size=16384"
    echo "       linker flag, and the openxr_loader_for_android version (need >=1.1.57)."
    exit 1
fi
printf '%s\n' "$ALIGN_REPORT"
# Smallest alignment across every library checked — the one that decides whether
# the AAR loads on a 16KB-page device.
MIN_ALIGN="$(printf '%s\n' "$ALIGN_REPORT" | sed -n 's/^OK  *.*: \(0x[0-9a-f]*\)$/\1/p' \
    | sort -u | python3 -c 'import sys; v=[int(l,16) for l in sys.stdin.read().split()]; print(min(v) if v else 0)')"
cd android

mkdir -p "$OUT_DIR"
# The JS package expects the file named viro_renderer-release.aar.
cp "$AAR_PATH" "$OUT_DIR/viro_renderer-release.aar"

echo "==> AAR staged: $OUT_DIR/viro_renderer-release.aar"
# sha256sum is coreutils; stock macOS ships shasum instead. Resolve once and
# fail loudly — inside "$(...)" a missing tool is swallowed and prints an empty
# hash, which reads as a successful build with no checksum.
if command -v sha256sum >/dev/null 2>&1; then
    AAR_SHA256="$(sha256sum "$OUT_DIR/viro_renderer-release.aar" | cut -d' ' -f1)"
elif command -v shasum >/dev/null 2>&1; then
    AAR_SHA256="$(shasum -a 256 "$OUT_DIR/viro_renderer-release.aar" | cut -d' ' -f1)"
else
    echo "ERROR: neither sha256sum nor shasum found; cannot record artifact provenance." >&2
    exit 1
fi
echo "    sha256: $AAR_SHA256"
echo ""
echo "Next: in the mikevocalz/viro JS fork,"
echo "  cp $OUT_DIR/viro_renderer-release.aar android/viro_renderer/viro_renderer-release.aar"
echo "  Rebuild Viro's react_viro bridge AAR against this renderer before packaging."
echo "  Validate the final APK and headset lifecycle before publishing a release."
python3 - "$OUT_DIR/viro_renderer-release.aar" "$REPO_ROOT" "$MIN_ALIGN" <<'PYMETA'
import hashlib, json, pathlib, subprocess, sys
artifact, root, min_align = pathlib.Path(sys.argv[1]), sys.argv[2], int(sys.argv[3])
# --untracked-files=no: dirty must mean the tracked sources drifted from the
# recorded commit. Untracked files do not change what was compiled, and the
# build itself leaves some behind, so counting them marks every artifact dirty
# and the flag stops carrying information.
dirty = subprocess.check_output(
    ["git", "-C", root, "status", "--porcelain", "--untracked-files=no"], text=True
).strip()
metadata = {
    "repository": "mikevocalz/virocore",
    "commit": subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip(),
    "dirty": bool(dirty),
    "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
    "artifact": artifact.name,
    "elf_alignment_verified": {"abi": "arm64-v8a", "min_p_align": min_align},
}
artifact.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
PYMETA
