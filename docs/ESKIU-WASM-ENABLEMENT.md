# Eskiu WebAssembly enablement

Eskiu v0.9.2's design explicitly cites LLVM WebAssembly support, but the shipped compiler
only registers/links AArch64, ARM and (where available) X86 code-generation backends.

That distinction matters: LLVM being able to target WASM is not the same as `eskiuc`
being built with the WebAssembly target.

## Viro patch

`tools/eskiu/patches/0001-enable-webassembly-backend.patch` is a pinned patch against
Eskiu v0.9.2 / commit `95d7ece7ea8d72bc106a141bc031a1648ed8f2ac`.

It does two things:

1. links the LLVM WebAssembly CodeGen/AsmParser/Desc/Info components;
2. registers the WebAssembly target, target info, target MC, asm printer and parser.

The CI probe builds only the `eskiuc` target, then compiles a runtime-free scalar Eskiu
function to `wasm32-unknown-unknown` and links the resulting object with `wasm-ld`.

## Why this stays a toolchain patch first

This PR does not yet put Eskiu code into `viro_web`. It proves that the compiler gap is
backend registration/linkage rather than a reason to redesign the Viro ABI.

After the probe is green, the next Web step is to compile one extension-owned engine
contract implementation to WASM and A/B it against the C++ reference implementation.

The patch should be dropped as soon as an upstream Eskiu release ships equivalent
WebAssembly backend support.
