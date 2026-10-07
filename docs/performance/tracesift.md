# TraceSift performance workflow

TraceSift is used here to analyze the JavaScript/React side of ViroCore performance investigations from a React Native or Viro consumer harness.

## Setup

Requires macOS or Linux and Node.js 22.19 or newer.

```sh
bash scripts/tracesift.sh init
bash scripts/tracesift.sh start
```

TraceSift runs locally. Keep provider credentials outside this repository.

## Scope

Good TraceSift inputs for ViroCore work include:

- Hermes CPU profiles captured while exercising a ViroCore scene through React Native;
- JavaScript CPU profiles from web/host harnesses;
- React Profiler exports showing expensive render/commit paths that trigger native scene mutations.

TraceSift does **not** analyze C++ CPU stacks, Metal/Vulkan GPU captures, OpenXR runtime traces, or platform compositor timings. For native renderer work, pair the TraceSift finding with Perfetto, Instruments, or the platform/GPU profiler. Treat the TraceSift result as the JS/React half of the investigation.

## Suggested scenarios

Keep the scenario fixed when comparing changes:

- large node-tree creation and teardown;
- repeated transform/material/property updates;
- input dispatch under drag, gaze, pointer, or controller load;
- external-surface frame updates;
- OpenXR scene entry/exit and frame-loop pressure;
- React-to-native bridge/JSI churn around scene mutation.

Run three comparable captures when a result is noisy and use the middle result rather than the best run.

Do not commit raw profiles by default. Summarize the hotspot, device/build, reproduction path, and before/after measurements in the PR.
