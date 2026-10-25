# rive-runtime (vendored)

Rive's C++ runtime and GPU renderer, linked statically into `libviro_renderer.so`
on Android arm64-v8a when `VIRO_WITH_RIVE` is ON. Build recipe, module choices and
the symbol-visibility check are in `docs/RIVE_RUNTIME.md`.

## Upstream

- Repository: https://github.com/rive-app/rive-runtime
- Commit: `8b3ca41b12bfd8ac9454444102c1fb130b3e7e11` (2026-10-24, "chore(downstream):
  advance submodule/refs"). The same hash is in `android/rive/RIVE_RUNTIME_COMMIT`.
- `.rive_head` (`bd41e84a02381a482b7fdc4eb4607ee58c2f2e6e`) is upstream's own record of
  the Rive monorepo revision it was exported from. It is not the pin.
- License: MIT, full text in `LICENSE` (Copyright (c) 2020 Rive).

## What is copied

Everything rive-runtime's premake build reads for the Android GLES static libraries:
`build/`, `dependencies/`, `decoders/`, `include/`, `renderer/` (`include/`, `src/`,
`glad/`, `premake5_pls_renderer.lua`, `LICENSE`, `README.md`), `scripting/`, `src/`,
`utils/`, `premake5_v2.lua`, `dummy.cpp`, `runtime.natvis`, `LICENSE`, `README.md`,
`.rive_head`, `.version`.

Left out: `tests/` (279 MB of test files and goldens), `dev/`, `skia/`, `rivinfo/`,
`Dockerfile`, `Doxyfile`, the upstream dotfiles (`.github/`, `.vscode/`, `.gitignore`,
`.gitattributes`, `.dockerignore`, `.lua-format`), and from `renderer/` the samples and
tool scripts (`path_fiddle/`, `renderdoc/`, `rive_vk_bootstrap/`, `shader_hotload/`,
`make_*.sh`, `premake5.lua`). ViroCore's replacement for `renderer/premake5.lua` is
`android/rive/premake5.lua`.

To check the copy against upstream, fetch the commit and compare:

```sh
git init -q /tmp/rr && cd /tmp/rr
git fetch --depth 1 https://github.com/rive-app/rive-runtime.git \
    8b3ca41b12bfd8ac9454444102c1fb130b3e7e11 && git checkout -q FETCH_HEAD
diff -rq -x .git /tmp/rr <virocore>/ViroRenderer/rive-runtime
```

The only file reported as differing should be `build/rive_build_config.lua`; everything
else should be listed as "Only in /tmp/rr" (the paths above), plus this `NOTICE.md`.

## Sources fetched at build time

premake fetches these into `android/rive/dependencies/` by the tag rive-runtime pins
(gitignored, never committed). Commits are what those tags resolved to on 2026-10-24.

| Source | Tag | Commit | License | Linked |
| --- | --- | --- | --- | --- |
| rive-app/harfbuzz | `rive_13.1.1` | `08d34675f64b1ac4880f4f10c9fd474a56dd4399` | Old MIT | yes |
| Tehreer/SheenBidi | `v2.6` | `adfccc46504b1be37b8894f064121c56c31312f7` | Apache 2.0 | yes |
| rive-app/yoga | `rive_yoga_fb91f6cb0f8f` | `9a6ac7fac43ca22d9b1e56468e5c8200ba0a07d3` | MIT | yes |
| rive-app/miniaudio | `rive_changes_8` | `2e9e1617c58e3f8c7182612054f90f0a37f95cae` | MIT-0 / public domain | headers only |
| dabeaz/ply | `3.11` | `0f398b72618c1564d71f7dc0558e6722b241875a` | BSD 3-Clause | shader build tool only |

`build_rive.sh` also clones premake-core `v5.0.0-beta7` and rive-app/premake-ninja
(`rive_modifications`) into `build/dependencies/` (gitignored).

## Local changes

One, kept as a patch in `../rive-runtime-patches/0001-accept-ndk-revision-override.patch`:

`build/rive_build_config.lua` refuses any NDK other than r27c (27.2.12479018). ViroCore
builds with 27.1.12297006 (r27b, `android/viroreact/build.gradle`). The patch lets
`RIVE_NDK_REVISION_OVERRIDE` name one more accepted `Pkg.Revision`, so the archives and
`libviro_renderer.so` come from the same toolchain. `scripts/build-rive-runtime.sh` sets
it to the Gradle `ndkVersion`. Both NDKs ship clang 18 (r522817).

To update: replace this directory with the new commit minus the paths listed above,
re-apply the patch (`git apply --directory=ViroRenderer/rive-runtime ...` from the repo
root), and change the hash here and in `android/rive/RIVE_RUNTIME_COMMIT`.
