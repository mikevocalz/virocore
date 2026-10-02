# Eskiu host C ABI smoke

This fixture is intentionally isolated from production ViroCore renderer code.

It verifies the minimum seam required by the Eskiu migration program:

1. scalar top-level Eskiu functions link beside C++;
2. C++ and Eskiu share the same POD memory layout through explicit pointers/views;
3. Eskiu can mutate caller-owned POD storage without taking ownership;
4. Eskiu can call an `extern "C"` function implemented by C++;
5. the probe compiles to a plain object and links beside C++.

## Why the pose crosses by pointer

The first version of this probe called a top-level Eskiu function with a seven-float
pose **by value** from C++. CI caught that the values did not round-trip correctly.

That is an important distinction in Eskiu v0.9.2:

- calls from Eskiu to an `extern` declaration receive explicit target C-ABI lowering;
- C callbacks into Eskiu can receive generated `__cabi_*` thunks when aggregate
  lowering requires them;
- ordinary Eskiu functions have their own aggregate-lowering rules internally.

Viro therefore does **not** make raw direct aggregate-by-value calls into Eskiu
implementation symbols. Cross-language engine data uses scalar values, opaque handles,
or pointer+length / pointer-to-POD views with explicit lifetime rules. If a future
integration intentionally uses an Eskiu-generated C callback thunk, that path gets its
own ABI conformance test.

## Run

Pin Eskiu v0.9.2, then:

```bash
ESKIUC=/path/to/eskiuc bash tests/eskiu/run_host_abi_smoke.sh
```

CI downloads the pinned v0.9.2 Linux x86-64 release and verifies its published SHA-256
before running the same script.

The script exits with code 2 when `eskiuc` is unavailable. It does not silently pass.

## Scope

This proves host scalar/pointer C ABI compatibility and shared POD memory layout. It does
**not** establish Android NDK, JNI, Quest, PICO, visionOS, iOS, WebAssembly, or production
renderer readiness.
