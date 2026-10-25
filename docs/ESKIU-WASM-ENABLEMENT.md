# Eskiu WebAssembly enablement

## Current state (Eskiu v0.14.0)

The release `eskiuc` registers LLVM's WebAssembly backend. Eskiu 0.13.0 lists
`wasm32-unknown-emscripten` (linked by `emcc`) and WASI as targets, and the pinned
v0.14.0 compiler emits a WebAssembly object for `wasm32-unknown-unknown` with
`--reloc static`.

The CI probe (`scripts/eskiu/probe-wasm-backend.sh`) installs the pinned release
archive, compiles a runtime-free scalar Eskiu function to `wasm32-unknown-unknown`
and links the object with `wasm-ld` from LLVM 22.

## History: the v0.9.2 patch

Eskiu v0.9.2's design cited LLVM WebAssembly support, but the shipped compiler only
registered/linked AArch64, ARM and (where available) X86 code-generation backends.
LLVM being able to target WASM was not the same as `eskiuc` being built with the
WebAssembly target.

The fork carried `tools/eskiu/patch-eskiu-wasm.py`, a fail-fast source patcher
pinned against v0.9.2 / commit `95d7ece7ea8d72bc106a141bc031a1648ed8f2ac`. It linked
the LLVM WebAssembly CodeGen/AsmParser/Desc/Info components and registered the
target, and the probe built `eskiuc` from patched source.

Upstream v0.14.0 makes the same changes in `CMakeLists.txt` and
`codegen/codegen_module.cpp`, so the patcher no longer applied and was removed
with the v0.14.0 pin.

## Next step

No Eskiu code is in `viro_web` yet. The next Web step is to compile one
extension-owned engine contract implementation to WASM and A/B it against the C++
reference implementation.
