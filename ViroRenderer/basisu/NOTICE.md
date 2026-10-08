# Basis Universal transcoder (vendored)

The renderer uses this code to decode glTF textures that carry the
`KHR_texture_basisu` extension (KTX2 files with ETC1S or UASTC payloads).
`VROKTX2Texture.cpp` is the only caller.

## Basis Universal

- Upstream: https://github.com/BinomialLLC/basis_universal
- Version: tag `v1_60`, commit `323239a6a5ffa57d6570cfc403be99156e33a8b0` (2025-01-21)
- License: Apache License 2.0, full text in `LICENSE`
- Copyright (C) 2019-2024 Binomial LLC
- Copied: every file in `transcoder/` (one backported fix, see Local changes). The encoder,
  tools and WebGL wrappers are not included.

## Zstandard decoder

Meta's runtime controller models supercompress their UASTC levels with Zstd
(KTX2 `supercompressionScheme` 2), so the decoder is needed.

- Source: the single-file decoder `zstd/zstddeclib.c` and `zstd/zstd.h`
  shipped in the same basis_universal commit (Zstandard 1.4.9 amalgamation)
- License: BSD 3-Clause, full text in `zstd/LICENSE`
- Copyright (c) 2016-present, Facebook, Inc.

## Local changes

One upstream fix is backported into `transcoder/basisu_containers.h`: the two
`assert(safe_shift_left(1ULL, ...))` calls in `hash_map` now pass
`static_cast<uint64_t>(1)`, as upstream does from `v2_0` on. With `1ULL`, the
call is ambiguous wherever `uint64_t` is `unsigned long` (Android arm64), so
debug builds fail to compile. No other upstream file is modified.

`CMakeLists.txt` in this directory is ours. It builds both libraries into the
static `viro_basisu` target with hidden symbol visibility and compiles out the
transcode targets the renderer never asks for (BC1-BC7, PVRTC1/2, ATC, FXT1,
EAC R11/RG11, UASTC HDR). To update, replace `transcoder/` and the two `zstd/`
sources from a newer tag, drop the backport if the tag already has it, and
edit the version lines above.
