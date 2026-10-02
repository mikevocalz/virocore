# ViroCore extension ABI

This directory is the language-neutral boundary for extension-owned renderer work.

## Rules

- Public/native bridges may depend on this ABI; they should not depend on an Eskiu implementation.
- C++ remains the reference renderer implementation.
- Eskiu v0.9.2 is the initial pinned experimental toolchain.
- ABI values are C-compatible POD data, borrowed views, or opaque handles.
- STL containers, C++ smart pointers/classes, Objective-C/Swift objects, Java/Kotlin objects, and native GPU object layouts never cross this boundary.
- A caller cannot retain a borrowed view after the call that supplied it returns.
- Owned memory is freed by the allocator/runtime that created it.
- High-rate frame/input APIs must use bounded storage or latest-value semantics.
- Every function added here must document threading and ownership.
- Structures intended to evolve begin with `struct_size`.
- Breaking binary changes increment the ABI major version.
- Production Eskiu replacements require the per-module ReactVision native-MCP audit and before/after benchmark described in `docs/ESKIU-EXTENSION-ARCHITECTURE.md`.

The initial ABI is deliberately data-only. Function tables are added after the C++ reference backend and conformance suite exist.
