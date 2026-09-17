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
if ! python3 scripts/verify-16kb-alignment.py "android/$AAR_PATH"; then
    echo "ERROR: AAR contains 4KB-aligned arm64-v8a libraries — refusing to stage."
    echo "       Check NDK version (need r27+), the -Wl,-z,max-page-size=16384"
    echo "       linker flag, and the openxr_loader_for_android version (need >=1.1.57)."
    exit 1
fi
cd android

mkdir -p "$OUT_DIR"
# The JS package expects the file named viro_renderer-release.aar.
cp "$AAR_PATH" "$OUT_DIR/viro_renderer-release.aar"

echo "==> AAR staged: $OUT_DIR/viro_renderer-release.aar"
echo "    sha256: $(sha256sum "$OUT_DIR/viro_renderer-release.aar" | cut -d' ' -f1)"
echo ""
echo "Next: in the mikevocalz/viro JS fork,"
echo "  cp $OUT_DIR/viro_renderer-release.aar android/viro_renderer/viro_renderer-release.aar"
echo "  Rebuild Viro's react_viro bridge AAR against this renderer before packaging."
echo "  Validate the final APK and headset lifecycle before publishing a release."
python3 - "$OUT_DIR/viro_renderer-release.aar" "$REPO_ROOT" <<'PYMETA'
import hashlib, json, pathlib, subprocess, sys
artifact, root = pathlib.Path(sys.argv[1]), sys.argv[2]
metadata = {
    "repository": "mikevocalz/virocore",
    "commit": subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip(),
    "dirty": bool(subprocess.check_output(["git", "-C", root, "status", "--porcelain"], text=True).strip()),
    "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
    "artifact": artifact.name,
    "elf_alignment_verified": "arm64-v8a >= 16384",
}
artifact.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
PYMETA
