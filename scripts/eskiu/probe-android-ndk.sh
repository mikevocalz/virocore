#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ESKIUC_BIN="${ESKIUC:-$(command -v eskiuc || true)}"
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
API="${ANDROID_API:-29}"
TARGET="aarch64-linux-android"
WORK="${TMPDIR:-/tmp}/viro-eskiu-android-probe"

if [[ -z "$ESKIUC_BIN" ]]; then
  echo "ERROR: eskiuc not found. Set ESKIUC=/path/to/eskiuc." >&2
  exit 2
fi
if [[ -z "$NDK_HOME" ]]; then
  echo "ERROR: ANDROID_NDK_HOME or ANDROID_NDK_ROOT is required." >&2
  exit 3
fi

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) HOST_TAG="darwin-x86_64" ;;
  Darwin-x86_64) HOST_TAG="darwin-x86_64" ;;
  Linux-x86_64) HOST_TAG="linux-x86_64" ;;
  Linux-aarch64) HOST_TAG="linux-x86_64" ;;
  *) echo "ERROR: unsupported host for this probe" >&2; exit 4 ;;
esac

TOOLCHAIN="$NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG"
CLANG="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang"
if [[ ! -x "$CLANG" ]]; then
  echo "ERROR: NDK clang not found at $CLANG" >&2
  exit 5
fi

rm -rf "$WORK"
mkdir -p "$WORK"

cat > "$WORK/probe.esk" <<'EOF'
struct ProbePair {
    int left;
    int right;
}

int viro_eskiu_android_add(ProbePair p) {
    return p.left + p.right;
}
EOF

echo "== Eskiu version =="
"$ESKIUC_BIN" --version

echo "== Attempt Android object emission =="
set +e
"$ESKIUC_BIN" "$WORK/probe.esk"   -c   -o "$WORK/probe.o"   --target "$TARGET"
ESKIU_STATUS=$?
set -e

if [[ $ESKIU_STATUS -ne 0 ]]; then
  cat <<EOF
RESULT: BLOCKED
Eskiu could not emit an object for $TARGET with the current toolchain.
This is an expected discovery outcome while Android remains unfinished upstream.
EOF
  exit 10
fi

echo "== Inspect object =="
file "$WORK/probe.o" || true

cat > "$WORK/driver.c" <<'EOF'
typedef struct ProbePair {
    int left;
    int right;
} ProbePair;

extern int viro_eskiu_android_add(ProbePair p);

int main(void) {
    ProbePair p = {20, 22};
    return viro_eskiu_android_add(p) == 42 ? 0 : 1;
}
EOF

echo "== Attempt NDK link =="
"$CLANG" "$WORK/driver.c" "$WORK/probe.o" -o "$WORK/probe"

cat <<EOF
RESULT: OBJECT_AND_LINK_OK
Target: $TARGET
API: $API
Artifact: $WORK/probe
NOTE: This still does not prove JNI, shared-library loading, Gradle integration,
16 KB page alignment, OpenXR integration, or device execution.
EOF
