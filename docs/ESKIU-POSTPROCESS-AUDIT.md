# Post-processing transfer audit

The post-process renderer remains GPU/shader driven. Named metrics measure CPU-side blit calls and texture work only.

Benchmark representative tone-map, blur, recording-blit and multi-texture workloads. If GPU shader time dominates, there is no reason to move the post-process path to Eskiu. Only measured CPU scratch/binding work is eligible for migration.
