#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ESKIUC_BIN="${ESKIUC:-$(command -v eskiuc || true)}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-apple.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [[ -z "$ESKIUC_BIN" ]]; then
  echo "error: eskiuc not found; set ESKIUC=/path/to/eskiuc" >&2
  exit 2
fi

"$ESKIUC_BIN" --version

probe_target() {
  local name="$1"
  local triple="$2"
  local object="$WORK/$name.o"

  echo "== $name: $triple =="
  "$ESKIUC_BIN" "$ROOT/tests/eskiu/apple_target_probe.esk"     -c -o "$object" --target "$triple"

  local description
  description="$(file "$object")"
  echo "$description"
  if [[ "$description" != *"Mach-O"* || "$description" != *"arm64"* ]]; then
    echo "error: expected an arm64 Mach-O object for $triple" >&2
    exit 3
  fi

  # GNU nm on Ubuntu does not consistently parse Mach-O. Symbol presence is
  # independently guaranteed by the Eskiu ABI suite; this probe is specifically
  # about target object emission/format.
}

probe_target ios "arm64-apple-ios17.0"
probe_target visionos "arm64-apple-xros1.0"

echo "Eskiu Apple target probe: PASS"
