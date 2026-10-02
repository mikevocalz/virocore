# Eskiu input ring shadow backend

This is the first executable Eskiu implementation behind an existing Viro engine-contract domain.

It deliberately does **not** replace the production C++ SPSC ring. The C++ implementation uses atomics and remains authoritative for renderer input until Eskiu's atomic/threading path is benchmarked under real producer/consumer load.

The shadow backend matches:
- the 96-byte `VROEngineInputSample` layout;
- FIFO behavior;
- bounded capacity;
- FULL/EMPTY/INVALID result codes;
- dropped-sample counting;
- zero allocation during push/pop after ring creation.

The CI fixture links the Eskiu object directly with a C++ consumer that uses the production ABI header. This catches layout and symbol drift.

Next gate: add an atomic SPSC Eskiu variant and run it through the engine benchmark harness before any runtime switch.
