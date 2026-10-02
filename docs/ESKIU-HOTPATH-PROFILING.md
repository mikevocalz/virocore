# Eskiu hot-path profiling gates

The remaining migration-manifest entries that were marked **profile first** now have opt-in timing/call-count gates:

- OpenGL post-processing: `VROImagePostProcessOpenGL::blit` and `blitOpt`
- physics: `VROPhysicsWorld::computePhysics`
- particles: `VROParticleEmitter::update`

Instrumentation uses the existing `VROEngineMetrics` substrate and therefore compiles to no-ops unless `VRO_ENGINE_METRICS_ENABLED=1`.

## Benchmark rule

The metric substrate is intentionally aggregate. Benchmark one candidate fixture at a time, reset the metric state before the fixture, run a fixed workload, then capture the snapshot. This keeps the ABI small and avoids permanently embedding module-specific counters in production.

A candidate only advances from **profile first** when its fixture shows a material CPU/allocation cost that an Eskiu implementation can plausibly improve. GPU-bound post-processing should stay native shader work rather than being migrated just for language consistency.
