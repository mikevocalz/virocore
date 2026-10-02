# Viro engine backend benchmark and A/B snapshot

This harness is the promotion gate for future Eskiu implementations.

## Snapshot v1

Every backend emits the same JSON shape:

- schema/version;
- backend id;
- engine ABI major/minor;
- peak process RSS when the host exposes it;
- named fixtures with operation count and p50/p95 ns per operation.

The initial C++ reference fixtures cover:
- input ring push/pop;
- rigid spatial transform composition;
- geometry validation;
- surface descriptor validation.

World-mesh and media fixtures are added once their ABI PRs land on `main`.

## A/B rule

`scripts/compare-engine-benchmarks.py` compares two snapshots by fixture name and fails
when p95 or RSS regression exceeds the requested threshold.

Default CI limits are intentionally conservative and are **not** the production Eskiu
promotion thresholds. A production migration must define its own workload-specific target
and include memory/copy correctness metrics relevant to that subsystem.

## Why batched p50/p95

A single wall-clock number is noisy and easy to game. Each fixture executes 41 batches,
sorts per-operation batch time, and records p50/p95. This is still a microbenchmark, so
device/end-to-end benchmarks remain required for renderer promotion.

## Backend rule

The benchmark binary currently links only the C++ reference backend. An Eskiu pilot adds
a second backend fixture and emits another snapshot; public React APIs are not involved.
