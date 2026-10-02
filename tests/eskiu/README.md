# Eskiu host C ABI smoke

This fixture is intentionally isolated from production ViroCore renderer code.

It verifies the minimum seam required by the Eskiu migration program:

1. a top-level Eskiu function is callable from C++;
2. an Eskiu POD struct passed/returned by value matches the C++ C-ABI view;
3. Eskiu can call an `extern "C"` function implemented by C++;
4. the probe can be compiled to a plain object and linked beside C++.

## Run

Pin Eskiu v0.9.2, then:

```bash
ESKIUC=/path/to/eskiuc bash tests/eskiu/run_host_abi_smoke.sh
```

The script exits with code 2 when `eskiuc` is unavailable. It does not silently pass.

## Scope

This proves only host C ABI compatibility. It does **not** establish Android NDK, JNI, Quest, PICO, visionOS, iOS, WebAssembly, or production renderer readiness.
