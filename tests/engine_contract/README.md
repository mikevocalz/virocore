# Engine contract foundation

This is the language-neutral renderer boundary used by the Eskiu migration program.

The first contract intentionally exposes only:
- semantic version;
- backend kind/name;
- build provenance;
- capability bits.

It is C-compatible so the reference C++ backend, Eskiu modules, JNI/ObjC++ bridges,
and WASM adapters can all consume the same shape without exposing C++ classes.

## Rules

- Callers initialize `struct_size`.
- New minor versions append fields rather than reordering existing fields.
- Strings are borrowed process-lifetime diagnostics.
- Capability bits describe implementation support, not product policy.
- Renderer-specific pointers/classes never cross this ABI.

Run the standalone conformance test with:

```bash
bash tests/engine_contract/run.sh
```
