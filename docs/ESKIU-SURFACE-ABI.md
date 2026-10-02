# Engine surface / texture exchange ABI

This contract is the common native seam for:
- `ViroGpuPanel`;
- `ViroRivePanel`;
- `ViroThreeJSPanel`;
- camera textures;
- video/recording surfaces;
- future WebGPU/Metal/OpenXR external producers.

## Opaque by design

The ABI never exposes:
- Metal texture objects;
- OpenGL texture classes;
- WebGPU objects;
- C++ renderer classes;
- Eskiu implementation objects.

A `VROEngineHandle` identifies a renderer-owned surface. Backend adapters decide how
that handle maps to Metal/OpenGL/WebGPU/native camera resources.

## Synchronization

`acquire_token` / `release_token` are opaque monotonic synchronization values.
The v0.1 contract does not assume semaphores, fences, events or API-specific primitives.

An adapter may map them to:
- Metal shared events;
- GL fences;
- Vulkan/OpenXR synchronization;
- WebGPU queue completion;
- a CPU frame counter for software/testing backends.

## React API stability

React components keep their existing props. They describe the producer/content; they do
not receive backend texture pointers. Separate adapter PRs will place the current
GPU/Rive/Three/camera implementations behind this contract one at a time.
