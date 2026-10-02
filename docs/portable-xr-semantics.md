# Portable XR semantic contract

ViroCore and alternate renderers such as the SPECS 27 Lens Studio backend do
not share implementation code. They do share application-visible semantics.

The machine-readable fixture at
`tests/portable_xr/semantic_contract.json` freezes the minimum cross-renderer
expectations for:

- public positions in meters;
- renderer-specific meter/centimeter conversion;
- Euler-degree JSX rotation to portable quaternion conversion;
- hit-test result normalization;
- ViroPolyline thickness/radius semantics;
- portable interaction phases;
- capability vocabulary.

## Boundary

This repository does **not** embed Lens Studio, SIK or Snap packages. Specs is
implemented outside ViroCore. These fixtures exist so ViroCore and the Lens
backend can be tested against the same externally visible behavior.

## Updating the contract

A semantic change should update all three places in the same workstream:

1. this fixture;
2. the React Viro public compiler/tests;
3. the alternate backend conformance suite.

Changing renderer internals alone must not silently alter this contract.
