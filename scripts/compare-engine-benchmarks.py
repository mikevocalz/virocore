#!/usr/bin/env python3
import argparse
import json
import sys

def load(path):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if data.get("schema") != "viro-engine-benchmark-v1":
        raise ValueError(f"{path}: unsupported benchmark schema")
    return data

def fixtures(data):
    return {item["name"]: item for item in data.get("fixtures", [])}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--max-p95-regression-percent", type=float, default=5.0)
    parser.add_argument("--max-rss-regression-percent", type=float, default=5.0)
    args = parser.parse_args()

    base = load(args.baseline)
    cand = load(args.candidate)
    bfx, cfx = fixtures(base), fixtures(cand)

    failures = []
    rows = []
    for name in sorted(bfx):
        if name not in cfx:
            failures.append(f"candidate missing fixture {name}")
            continue
        b = float(bfx[name]["p95_ns_per_op"])
        c = float(cfx[name]["p95_ns_per_op"])
        delta = 0.0 if b == 0 else ((c - b) / b) * 100.0
        rows.append((name, b, c, delta))
        if delta > args.max_p95_regression_percent:
            failures.append(
                f"{name}: p95 regression {delta:.2f}% > "
                f"{args.max_p95_regression_percent:.2f}%")

    brss = int(base.get("peak_rss_bytes", 0) or 0)
    crss = int(cand.get("peak_rss_bytes", 0) or 0)
    if brss and crss:
        delta = ((crss - brss) / brss) * 100.0
        if delta > args.max_rss_regression_percent:
            failures.append(
                f"peak RSS regression {delta:.2f}% > "
                f"{args.max_rss_regression_percent:.2f}%")

    print(f"baseline={base.get('backend')} candidate={cand.get('backend')}")
    for name, b, c, d in rows:
        print(f"{name}: p95 {b:.3f} -> {c:.3f} ns/op ({d:+.2f}%)")
    if brss and crss:
        print(f"peak_rss_bytes: {brss} -> {crss}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print("Benchmark comparison: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
