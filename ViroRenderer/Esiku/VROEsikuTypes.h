#pragma once

#include <cstdint>
#include <string>

namespace VRO {

enum class VROEsikuBackend : uint8_t {
  OpenXR,
  VitureNative,
  RayNeoNative,
  Wearable,
};

enum class VROEsikuCapability : uint8_t {
  StereoDisplay,
  HeadPose,
  Pose6DoF,
  HandTracking,
  EyeGaze,
  Passthrough,
  Camera,
  Depth,
  Planes,
  SceneUnderstanding,
  WorldAnchors,
  Controller,
  Touch,
  Gesture,
  Haptics,
  SpatialAudio,
  Microphone,
  VoiceInput,
  VoiceOutput,
  WearableDisplay,
  DeviceControl,
};

enum class VROEsikuCapabilityState : uint8_t {
  Supported,
  Unsupported,
  PermissionRequired,
  TemporarilyUnavailable,
  Unverified,
};

struct VROEsikuCapability {
  VROEsikuCapability capability;
  VROEsikuCapabilityState state;
};

struct VROEsikuDeviceProfile {
  VROEsikuBackend backend = VROEsikuBackend::OpenXR;
  std::string runtime;
  std::string model;
};

struct VROEsikuFrame {
  bool hasHeadPose = false;
  double timestampNs = 0.0;
};

/**
 * Native ViroCore seam for Esiku.
 *
 * The renderer remains responsible for graphics and frame timing. Esiku owns
 * vendor/runtime negotiation and reports capabilities into this POD boundary.
 * No vendor SDK types cross this header.
 */
class VROEsikuRuntime {
 public:
  virtual ~VROEsikuRuntime() = default;

  virtual VROEsikuDeviceProfile negotiate() = 0;
  virtual bool beginSession() = 0;
  virtual bool pollFrame(VROEsikuFrame& frame) = 0;
  virtual void endSession() = 0;
};

}  // namespace VRO
