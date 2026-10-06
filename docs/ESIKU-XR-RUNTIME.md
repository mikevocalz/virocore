# Esiku XR runtime integration

This PR establishes the native ABI boundary; it intentionally does not fork
the OpenXR renderer per product.

## Device mapping

- Android XR: OpenXR
- XREAL AURA: OpenXR
- RayNeo: OpenXR first; RayNeo-native extensions only where needed
- VITURE: native SDK adapter
- HTC VIVE Eagle: wearable adapter
- Valve Steam Frame: OpenXR + runtime interaction-profile negotiation
- Quest: existing OpenXR renderer
- Vision Pro: existing visionOS renderer

## Required native skills

- OpenXR 1.x frame/session lifecycle and extension negotiation.
- Android XR runtime capability and permission negotiation.
- ViroCore C++ render-thread ownership and existing OpenXR renderer lifecycle.
- JNI/JSI capability bridge design with stable ABI and no per-frame JS work.
- External GPU surface ownership (SurfaceTexture/AHardwareBuffer on Android).
- VITURE SDK native integration and lifecycle isolation.
- RayNeo OpenXR/ARDK integration with capability fallback.
- VIVE Eagle wearable SDK transport and audio/camera ownership.
- Steam Frame OpenXR interaction profiles and controller lifecycle.
- Device-lab validation across Quest, Android XR/XREAL, RayNeo, VITURE and
  Steam Frame, plus wearable transport tests for VIVE Eagle.

## Non-goals

- No device-specific renderer subclasses.
- No WebView rendering.
- No screenshot/bitmap bridge.
- No JavaScript frame stepping.
- No vendor SDK headers in the public ViroCore Esiku interface.
