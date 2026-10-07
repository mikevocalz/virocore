#!/usr/bin/env bash
set -euo pipefail

required_major=22
required_minor=19

if ! command -v node >/dev/null 2>&1; then
  echo "TraceSift requires Node.js >=22.19.0." >&2
  exit 1
fi

node -e '
const [major, minor] = process.versions.node.split(".").map(Number);
if (major < 22 || (major === 22 && minor < 19)) {
  console.error(`TraceSift requires Node.js >=22.19.0; found ${process.versions.node}.`);
  process.exit(1);
}
'

exec npx --yes @callstack/tracesift@0.3.1 "$@"
