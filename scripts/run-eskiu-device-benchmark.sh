#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run-eskiu-device-benchmark.sh --platform P --device D --runtime R     --backend cpp|eskiu --workload NAME --command '...'

The benchmarked command must emit a single JSON object on stdout. The runner
adds provenance fields and writes a normalized artifact under artifacts/eskiu-benchmarks.
EOF
}

platform=""; device=""; runtime=""; backend=""; workload=""; command=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --platform) platform="$2"; shift 2 ;;
    --device) device="$2"; shift 2 ;;
    --runtime) runtime="$2"; shift 2 ;;
    --backend) backend="$2"; shift 2 ;;
    --workload) workload="$2"; shift 2 ;;
    --command) command="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for v in platform device runtime backend workload command; do
  if [[ -z "${!v}" ]]; then echo "missing --$v" >&2; exit 2; fi
done
if [[ "$backend" != "cpp" && "$backend" != "eskiu" ]]; then
  echo "--backend must be cpp or eskiu" >&2; exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT/artifacts/eskiu-benchmarks"
mkdir -p "$OUT_DIR"
sha="$(git -C "$ROOT" rev-parse HEAD)"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
safe_device="$(printf '%s' "$device" | tr ' /:' '___')"
out="$OUT_DIR/${stamp}-${platform}-${safe_device}-${workload}-${backend}.json"

payload="$(bash -lc "$command")"
python3 - "$payload" "$out" "$platform" "$device" "$runtime" "$backend" "$workload" "$sha" <<'PY'
import json, pathlib, sys
payload, out, platform, device, runtime, backend, workload, sha = sys.argv[1:]
data = json.loads(payload)
if not isinstance(data, dict):
    raise SystemExit("benchmark command must emit a JSON object")
base = {
    "schemaVersion": 1,
    "platform": platform,
    "device": device,
    "runtime": runtime,
    "backend": backend,
    "workload": workload,
    "gitSha": sha,
}
base.update(data)
required = ["iterations"]
missing = [k for k in required if k not in base]
if missing:
    raise SystemExit("missing benchmark fields: " + ", ".join(missing))
path = pathlib.Path(out)
path.write_text(json.dumps(base, indent=2, sort_keys=True) + "\n")
print(path)
PY
