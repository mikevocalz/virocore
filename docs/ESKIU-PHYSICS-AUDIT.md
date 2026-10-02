# Physics transfer audit

This instruments the existing Bullet-backed physics path; it does not approve or perform an Eskiu rewrite.

With `VRO_ENGINE_METRICS_ENABLED=1`, `VROPhysicsWorld::computePhysics` records:
- timed physics subsystem calls;
- active physics-body count as work items;
- Bullet contact-manifold count as work items.

## Decision gate

Benchmark idle, 10/100/500 bodies, collision-heavy scenes, and world-mesh collision bodies on mobile/Quest/PICO.

If Bullet simulation dominates, keep Bullet/C++ and only move measured bookkeeping or transfer work. Eskiu should not replace code merely for language consistency.
