# Eskiu Apple target enablement

This gate distinguishes generic AArch64 support from actual Apple object-target support.

The probe asks pinned Eskiu v0.9.2 to emit object files for:

- `arm64-apple-ios17.0`
- `arm64-apple-xros1.0`

and requires both outputs to be arm64 Mach-O objects.

## What a green probe proves

- the compiler can select the AArch64 Mach-O backend for iOS;
- the compiler can select the AArch64 Mach-O backend for visionOS/xros;
- extension-owned Eskiu code can be compiled into objects suitable for a later Apple
  linker/Xcode integration experiment.

## What it does not prove

- Xcode/CocoaPods integration;
- linking against iOS or visionOS SDK frameworks;
- Swift/ObjC++ bridge lifecycle;
- Metal resource interop;
- App Store/device execution.

Those are the next gates. Production ViroKit code remains C++/ObjC++ until a module-level
native-MCP audit and device A/B benchmark are complete.
