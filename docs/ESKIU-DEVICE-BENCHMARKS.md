# Eskiu device A/B benchmark runner

This is the evidence bridge between the already-landed host/ABI/conformance work and a production renderer swap.

A production swap is not approved by a successful build alone. For each candidate module, capture the **same workload** with `cpp` and `eskiu` backends on the same physical device/runtime.

## Required order

1. Run the C++ baseline.
2. Run the Eskiu shadow/backend with identical scene/input/assets.
3. Compare artifacts with `scripts/compare-engine-benchmarks.py`.
4. Attach both JSON artifacts and the comparison to the production-swap PR.
5. Repeat on every target the module ships on.

## Examples

Android / Quest / PICO:
```bash
bash scripts/run-eskiu-device-benchmark.sh \
  --platform android --device "Quest 3" --runtime OpenXR \
  --backend cpp --workload input-ring \
  --command './device-benchmark --json --backend=cpp'
```

Apple:
```bash
bash scripts/run-eskiu-device-benchmark.sh \
  --platform visionos --device "Vision Pro" --runtime Metal \
  --backend eskiu --workload dynamic-geometry \
  --command './device-benchmark --json --backend=eskiu'
```

Web:
```bash
bash scripts/run-eskiu-device-benchmark.sh \
  --platform web --device "Chrome desktop" --runtime wasm \
  --backend eskiu --workload world-mesh \
  --command 'node ./wasm-benchmark.mjs --json'
```

The runner intentionally does not invent measurements. It normalizes provenance and stores whatever the platform harness actually measured.
