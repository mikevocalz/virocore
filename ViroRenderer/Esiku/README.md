# Esiku runtime seam

This directory defines the native ViroCore boundary for Esiku-backed hardware.

## Architecture

ViroCore remains the renderer. Esiku is the runtime/device capability bridge:

Viro / React Native
  -> ViroCore
  -> Esiku runtime
     -> OpenXR
        -> Android XR / XREAL AURA / RayNeo / Steam Frame / Quest
     -> VITURE native adapter
     -> wearable adapter
        -> HTC VIVE Eagle

The OpenXR lane is deliberately shared. A new Android XR glasses model must
not require a new ViroCore renderer.

## Contract rules

- Vendor SDK types never cross the VROEsikuRuntime interface.
- Runtime negotiation happens before feature gating.
- Per-frame data is native and render-thread safe; JavaScript does not drive
  frame stepping.
- External GPU surfaces continue through the existing ViroCore native surface
  path.
- Unsupported capabilities are reported rather than emulated with screenshots
  or WebViews.

## Follow-up implementation

The next native PR wires this seam into the existing VROSceneRendererOpenXR
session and JNI capability query path. Vendor adapters remain separate from
the renderer.
