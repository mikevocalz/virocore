# Physics transfer audit

This PR instruments the existing Bullet-backed physics path; it does not approve or perform
an Eskiu rewrite.

With `VRO_ENGINE_METRICS_ENABLED=1`, `VROPhysicsWorld::computePhysics` records:
- one timed subsystem call per physics step;
- active physics-body count as work items;
- Bullet contact-manifold count as additional work items.

With metrics disabled, the macros compile to no-ops.

## Decision gate

Capture representative fixtures before proposing a language migration:
- idle world;
- 10 / 100 / 500 active bodies;
- collision-heavy scene;
- world-mesh collision bodies;
- Quest/PICO/mobile CPU frame budget.

If the dominant cost is Bullet simulation itself, keep the physics engine in C++ and only
consider ownership/transfer cleanup. Eskiu is only approved for a measured boundary or
bookkeeping stage with a clear memory/CPU benefit.
