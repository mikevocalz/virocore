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
#include <cstddef>
#include <cstdint>
#include <limits>

// Fixed-foveation level for XR_FB_foveation. The integer values are the codes
// com.viro.core.ViroViewOpenXR passes across JNI; keep them in this order.
enum class VROOpenXRFoveationLevel { OFF = 0, LOW = 1, MEDIUM = 2, HIGH = 3 };

// Eye-buffer scale applied when nothing else is configured. The recommended
// rect is tuned for 3D scenes; flat UI panels with small text alias and blur
// at that density, so the default renders 1.3x per axis.
constexpr float kVROEyeBufferScaleDefault = 1.3f;

// Display refresh rate request for XR_FB_display_refresh_rate. The integer
// values are the codes com.viro.core.Renderer passes across JNI.
//   NONE    - no request; the runtime keeps its default rate. This is the
//             default, and with it xrRequestDisplayRefreshRateFB is never called.
//   HIGHEST - the highest rate xrEnumerateDisplayRefreshRatesFB reports.
//   HZ      - the closest supported rate at or below `hz`.
enum class VRODisplayRefreshRateMode { NONE = 0, HIGHEST = 1, HZ = 2 };

struct VRODisplayRefreshRateRequest {
    VRODisplayRefreshRateMode mode = VRODisplayRefreshRateMode::NONE;
    // Only read when mode is HZ.
    float hz = 0.0f;

    static VRODisplayRefreshRateRequest highest() {
        VRODisplayRefreshRateRequest r;
        r.mode = VRODisplayRefreshRateMode::HIGHEST;
        return r;
    }
    static VRODisplayRefreshRateRequest atMostHz(float value) {
        VRODisplayRefreshRateRequest r;
        r.mode = VRODisplayRefreshRateMode::HZ;
        r.hz = value;
        return r;
    }
};

// What the app asked for at renderer creation. The eye-buffer scale is read
// once, when the session creates its swapchains; the foveation fields are the
// level applied right after that, which setFoveationLevel() can change later.
// The display refresh rate is requested each time the session begins.
struct VROOpenXRRenderQuality {
    float eyeBufferScale = kVROEyeBufferScaleDefault;
    VROOpenXRFoveationLevel foveationLevel = VROOpenXRFoveationLevel::LOW;
    bool foveationDynamic = true;
    VRODisplayRefreshRateRequest displayRefreshRate;
};

// How a refresh-rate request resolved against the runtime's supported rates.
enum class VRODisplayRefreshRateOutcome {
    NO_REQUEST,          // mode NONE: do not call xrRequestDisplayRefreshRateFB
    INVALID_REQUEST,     // mode HZ with a NaN, infinite, zero or negative value
    NO_SUPPORTED_RATES,  // the enumeration held no positive finite rate
    CHOSEN,              // rateHz is the highest rate, or the closest not above hz
    BELOW_ALL_RATES,     // hz is below every supported rate; rateHz is the lowest
};

struct VRODisplayRefreshRateChoice {
    VRODisplayRefreshRateOutcome outcome = VRODisplayRefreshRateOutcome::NO_REQUEST;
    // The rate to pass to xrRequestDisplayRefreshRateFB. Only meaningful when
    // shouldRequest() is true; it is always one of the enumerated rates there,
    // because the runtime rejects any other value with
    // XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB.
    float rateHz = 0.0f;

    bool shouldRequest() const {
        return outcome == VRODisplayRefreshRateOutcome::CHOSEN ||
               outcome == VRODisplayRefreshRateOutcome::BELOW_ALL_RATES;
    }
};

// Requests within this many Hz above a supported rate still match it, so a
// request of 89.999 picks a reported 90.0.
constexpr float kVRODisplayRefreshRateMatchToleranceHz = 0.01f;

// Pick the rate to request from the rates xrEnumerateDisplayRefreshRatesFB
// returned. The spec orders that list lowest to highest; this does not rely on
// it, and skips entries that are not positive and finite.
inline VRODisplayRefreshRateChoice VROPickDisplayRefreshRate(
        const VRODisplayRefreshRateRequest &request,
        const float *rates, size_t rateCount) {
    VRODisplayRefreshRateChoice c;
    if (request.mode == VRODisplayRefreshRateMode::NONE) {
        c.outcome = VRODisplayRefreshRateOutcome::NO_REQUEST;
        return c;
    }
    if (request.mode == VRODisplayRefreshRateMode::HZ &&
        !(std::isfinite(request.hz) && request.hz > 0.0f)) {
        c.outcome = VRODisplayRefreshRateOutcome::INVALID_REQUEST;
        return c;
    }

    bool any = false;
    bool anyAtOrBelow = false;
    float lowest = 0.0f, highest = 0.0f, bestAtOrBelow = 0.0f;
    const float ceiling = request.hz + kVRODisplayRefreshRateMatchToleranceHz;
    for (size_t i = 0; rates != nullptr && i < rateCount; ++i) {
        const float r = rates[i];
        if (!(std::isfinite(r) && r > 0.0f)) {
            continue;
        }
        if (!any) {
            lowest = highest = r;
            any = true;
        } else {
            lowest = std::min(lowest, r);
            highest = std::max(highest, r);
        }
        if (r <= ceiling && (!anyAtOrBelow || r > bestAtOrBelow)) {
            bestAtOrBelow = r;
            anyAtOrBelow = true;
        }
    }

    if (!any) {
        c.outcome = VRODisplayRefreshRateOutcome::NO_SUPPORTED_RATES;
        return c;
    }
    if (request.mode == VRODisplayRefreshRateMode::HIGHEST) {
        c.outcome = VRODisplayRefreshRateOutcome::CHOSEN;
        c.rateHz = highest;
        return c;
    }
    if (anyAtOrBelow) {
        c.outcome = VRODisplayRefreshRateOutcome::CHOSEN;
        c.rateHz = bestAtOrBelow;
        return c;
    }
    c.outcome = VRODisplayRefreshRateOutcome::BELOW_ALL_RATES;
    c.rateHz = lowest;
    return c;
}

// Map the JNI (mode, hz) pair to a request. Returns false and leaves *out
// untouched for a mode outside 0..2. The hz value is not validated here;
// VROPickDisplayRefreshRate reports INVALID_REQUEST for a bad one.
inline bool VRODisplayRefreshRateRequestFromCode(int mode, float hz,
                                                 VRODisplayRefreshRateRequest *out) {
    switch (mode) {
        case 0: *out = VRODisplayRefreshRateRequest();           return true;
        case 1: *out = VRODisplayRefreshRateRequest::highest();  return true;
        case 2: *out = VRODisplayRefreshRateRequest::atMostHz(hz);     return true;
        default: return false;
    }
}

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
