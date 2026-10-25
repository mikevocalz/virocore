//
//  VROOpenXRRenderQuality.h
//  ViroRenderer
//
//  Startup render-quality settings for the OpenXR renderer (eye-buffer scale
//  and the initial fixed-foveation level), plus the pure math that turns a
//  requested eye-buffer scale into per-eye swapchain sizes.
//
//  This header has no GL, EGL or OpenXR dependency so the host test
//  (scripts/test-eye-buffer-scale.sh) can compile it with the host toolchain.
//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Fixed-foveation level for XR_FB_foveation. The integer values are the codes
// com.viro.core.ViroViewOpenXR passes across JNI; keep them in this order.
enum class VROOpenXRFoveationLevel { OFF = 0, LOW = 1, MEDIUM = 2, HIGH = 3 };

// Eye-buffer scale applied when nothing else is configured. The recommended
// rect is tuned for 3D scenes; flat UI panels with small text alias and blur
// at that density, so the default renders 1.3x per axis.
constexpr float kVROEyeBufferScaleDefault = 1.3f;

// What the app asked for at renderer creation. The eye-buffer scale is read
// once, when the session creates its swapchains; the foveation fields are the
// level applied right after that, which setFoveationLevel() can change later.
struct VROOpenXRRenderQuality {
    float eyeBufferScale = kVROEyeBufferScaleDefault;
    VROOpenXRFoveationLevel foveationLevel = VROOpenXRFoveationLevel::LOW;
    bool foveationDynamic = true;
};

// The outcome of resolving a requested scale against one eye's view
// configuration. Everything here is logged, so a device log shows what was
// asked for, what the clamp changed and what the swapchain got.
struct VROEyeBufferResolution {
    // The scale that was resolved: the request, or the default when the
    // request was not finite.
    float requestedScale = kVROEyeBufferScaleDefault;
    // True when the request was NaN or infinite and the default was used.
    bool rejectedNonFinite = false;
    // Scale actually applied on each axis after clamping to
    // [1, maxImageRect / recommendedImageRect].
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    // recommended * requestedScale before any clamp, saturated to uint32.
    uint32_t requestedWidth = 0;
    uint32_t requestedHeight = 0;
    // Swapchain size to create.
    uint32_t width = 0;
    uint32_t height = 0;
};

namespace VROOpenXRRenderQualityDetail {

inline uint32_t saturatingRound(double value) {
    if (!(value > 0.0)) {
        return 0;
    }
    const double maxValue = (double) std::numeric_limits<uint32_t>::max();
    if (value >= maxValue) {
        return std::numeric_limits<uint32_t>::max();
    }
    return (uint32_t) (value + 0.5);
}

// One axis. The lower bound wins over the upper one when a runtime reports a
// max below the recommended size (a spec violation): the result is still
// capped at max, because the runtime rejects anything larger.
inline uint32_t resolveAxis(double scale, uint32_t recommended, uint32_t maxSize,
                            float *outAppliedScale) {
    if (recommended == 0) {
        *outAppliedScale = 1.0f;
        return 0;
    }
    const double headroom = (double) maxSize / (double) recommended;
    const double applied = std::max(1.0, std::min(scale, headroom));
    const uint32_t size = std::min(maxSize, saturatingRound(recommended * applied));
    *outAppliedScale = (float) ((double) size / (double) recommended);
    return size;
}

}  // namespace VROOpenXRRenderQualityDetail

// Resolve the swapchain size for one eye.
//
//   - A non-finite request is rejected and kVROEyeBufferScaleDefault is used.
//   - A finite request below 1.0 (including 0 and negatives) clamps to 1.0,
//     which is the recommended size.
//   - Each axis clamps independently to maxImageRect / recommendedImageRect,
//     so one axis can hit the runtime limit while the other keeps the request.
inline VROEyeBufferResolution VROResolveEyeBufferSize(float requestedScale,
                                                      uint32_t recommendedWidth,
                                                      uint32_t recommendedHeight,
                                                      uint32_t maxWidth,
                                                      uint32_t maxHeight) {
    using namespace VROOpenXRRenderQualityDetail;
    VROEyeBufferResolution r;
    r.rejectedNonFinite = !std::isfinite(requestedScale);
    r.requestedScale = r.rejectedNonFinite ? kVROEyeBufferScaleDefault : requestedScale;

    const double scale = (double) r.requestedScale;
    r.requestedWidth = saturatingRound(recommendedWidth * scale);
    r.requestedHeight = saturatingRound(recommendedHeight * scale);
    r.width = resolveAxis(scale, recommendedWidth, maxWidth, &r.scaleX);
    r.height = resolveAxis(scale, recommendedHeight, maxHeight, &r.scaleY);
    return r;
}

// Map a JNI foveation code to a level. Returns false and leaves *out untouched
// for a code outside 0..3.
inline bool VROFoveationLevelFromCode(int code, VROOpenXRFoveationLevel *out) {
    switch (code) {
        case 0: *out = VROOpenXRFoveationLevel::OFF;    return true;
        case 1: *out = VROOpenXRFoveationLevel::LOW;    return true;
        case 2: *out = VROOpenXRFoveationLevel::MEDIUM; return true;
        case 3: *out = VROOpenXRFoveationLevel::HIGH;   return true;
        default: return false;
    }
}
