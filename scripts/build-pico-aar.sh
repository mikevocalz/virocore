#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$REPO_ROOT/dist-aar"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out)
      [[ $# -ge 2 ]] || { echo "--out requires a directory"; exit 2; }
      OUT_DIR="$2"
      shift 2
      ;;
    *)
      echo "unknown arg: $1"
      exit 2
      ;;
  esac
done

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd -- "$OUT_DIR" && pwd)"
cd "$REPO_ROOT"

echo "==> Building patched virocore renderer AAR"
echo "    branch: $(git rev-parse --abbrev-ref HEAD)  commit: $(git rev-parse --short HEAD)"

grep -q "bytedance/pico4s_controller" android/sharedCode/src/main/cpp/VROInputControllerOpenXR.cpp   || { echo "ERROR: PICO interaction profiles missing"; exit 1; }
grep -q "VROOpenXRRuntimeInfo" android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.h   || { echo "ERROR: runtime-info struct missing"; exit 1; }

cd android
./gradlew :viroreact:assembleRelease

AAR_PATH="viroreact/build/outputs/aar/viroreact-release.aar"
[[ -f "$AAR_PATH" ]] || { echo "ERROR: AAR not produced at $AAR_PATH"; exit 1; }

echo "==> Verifying 16KB page alignment of arm64-v8a libraries"
cd ..
if ! ALIGN_REPORT="$(python3 scripts/verify-16kb-alignment.py "android/$AAR_PATH")"; then
  printf '%s\n' "$ALIGN_REPORT"
  echo "ERROR: AAR failed the 16KB ELF alignment gate."
  exit 1
fi
printf '%s\n' "$ALIGN_REPORT"
MIN_ALIGN="$(printf '%s\n' "$ALIGN_REPORT" | sed -n 's/^OK  *.*: \(0x[0-9a-f]*\)$/\1/p'   | sort -u | python3 -c 'import sys; v=[int(l,16) for l in sys.stdin.read().split()]; print(min(v) if v else 0)')"

cp "android/$AAR_PATH" "$OUT_DIR/viro_renderer-release.aar"

if command -v sha256sum >/dev/null 2>&1; then
  AAR_SHA256="$(sha256sum "$OUT_DIR/viro_renderer-release.aar" | cut -d' ' -f1)"
elif command -v shasum >/dev/null 2>&1; then
  AAR_SHA256="$(shasum -a 256 "$OUT_DIR/viro_renderer-release.aar" | cut -d' ' -f1)"
else
  echo "ERROR: neither sha256sum nor shasum found." >&2
  exit 1
fi

echo "==> AAR staged: $OUT_DIR/viro_renderer-release.aar"
echo "    sha256: $AAR_SHA256"

python3 - "$OUT_DIR/viro_renderer-release.aar" "$REPO_ROOT" "$MIN_ALIGN" <<'PYMETA'
import hashlib, json, pathlib, subprocess, sys
artifact, root, min_align = pathlib.Path(sys.argv[1]), sys.argv[2], int(sys.argv[3])
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
