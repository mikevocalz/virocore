# Particle transfer audit

The current particle path remains C++/GPU-backed. This audit adds named opt-in timing and work-item metrics for the CPU-side emitter update.

Record representative fixtures at low, medium and maximum particle counts, with bursts and modifiers enabled. Compare CPU update time and allocation pressure separately from GPU/UBO upload time.

If rendering/upload dominates, keep the renderer path as-is. Eskiu is only a candidate for measured CPU-side bookkeeping or buffer preparation that improves memory or frame time.
