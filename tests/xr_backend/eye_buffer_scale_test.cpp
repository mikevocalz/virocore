// Host test for VROOpenXRRenderQuality.h: how a requested eye-buffer scale
// becomes per-eye swapchain sizes, and how foveation codes from Java map to
// levels. The header is GL/XR-free, so this compiles with the host toolchain.
#include "VROOpenXRRenderQuality.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    // Quest 3 recommended rect per eye, and a runtime max of 2x.
    const uint32_t recW = 1680, recH = 1760, maxW = 3360, maxH = 3520;

    // default: 1.3x, unclamped
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(
            kVROEyeBufferScaleDefault, recW, recH, maxW, maxH);
        check(near(kVROEyeBufferScaleDefault, 1.3f), "default scale is 1.3");
        check(r.width == 2184 && r.height == 2288, "default size is 1.3x recommended");
        check(r.requestedWidth == 2184 && r.requestedHeight == 2288, "default requested size");
        check(near(r.scaleX, 1.3f) && near(r.scaleY, 1.3f), "default applied scale");
        check(!r.rejectedNonFinite, "default is not rejected");
    }

    // override inside range
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(1.5f, recW, recH, maxW, maxH);
        check(r.width == 2520 && r.height == 2640, "1.5x override");
        check(near(r.requestedScale, 1.5f), "requested scale echoed");
    }

    // exactly 1.0 renders at the recommended size
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(1.0f, recW, recH, maxW, maxH);
        check(r.width == recW && r.height == recH, "1.0x is the recommended size");
    }

    // below 1 clamps up to 1, including zero and negatives
    {
        const float lows[] = { 0.5f, 0.0f, -2.0f, std::numeric_limits<float>::denorm_min() };
        for (float s : lows) {
            VROEyeBufferResolution r = VROResolveEyeBufferSize(s, recW, recH, maxW, maxH);
            check(r.width == recW && r.height == recH, "below 1 clamped to recommended");
            check(near(r.scaleX, 1.0f) && near(r.scaleY, 1.0f), "below 1 applied scale is 1");
            check(!r.rejectedNonFinite, "finite low value is clamped, not rejected");
        }
    }

    // above max clamps per axis: width room 1.4x, height room 2x
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(1.8f, 1000, 1000, 1400, 2000);
        check(r.width == 1400, "width clamped to max");
        check(r.height == 1800, "height keeps 1.8x");
        check(near(r.scaleX, 1.4f) && near(r.scaleY, 1.8f), "per-axis applied scales");
        check(r.requestedWidth == 1800 && r.requestedHeight == 1800, "requested size before clamp");
    }

    // huge finite request saturates the requested size and clamps to max
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(1e30f, recW, recH, maxW, maxH);
        check(r.width == maxW && r.height == maxH, "huge scale clamps to max");
        check(r.requestedWidth == std::numeric_limits<uint32_t>::max(), "requested size saturates");
    }

    // non-finite is rejected and the default applies
    {
        const float bad[] = { std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity() };
        for (float s : bad) {
            VROEyeBufferResolution r = VROResolveEyeBufferSize(s, recW, recH, maxW, maxH);
            check(r.rejectedNonFinite, "non-finite flagged");
            check(r.width == 2184 && r.height == 2288, "non-finite falls back to 1.3x");
            check(near(r.requestedScale, kVROEyeBufferScaleDefault), "non-finite reports the default");
        }
    }

    // recommended == max: no headroom, every scale renders at max
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(1.3f, 1440, 1584, 1440, 1584);
        check(r.width == 1440 && r.height == 1584, "recommended == max stays at max");
        check(near(r.scaleX, 1.0f) && near(r.scaleY, 1.0f), "recommended == max applied 1.0");
    }

    // a runtime reporting max below recommended (spec violation) never exceeds max
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(1.0f, 2000, 2000, 1500, 1500);
        check(r.width == 1500 && r.height == 1500, "max below recommended caps at max");
    }

    // zero recommended rect (broken runtime) yields zero, no division by zero
    {
        VROEyeBufferResolution r = VROResolveEyeBufferSize(1.3f, 0, 0, 0, 0);
        check(r.width == 0 && r.height == 0, "zero recommended stays zero");
    }

    // foveation codes, 0..3 in Java order
    {
        VROOpenXRFoveationLevel level = VROOpenXRFoveationLevel::HIGH;
        check(VROFoveationLevelFromCode(0, &level) && level == VROOpenXRFoveationLevel::OFF, "code 0 OFF");
        check(VROFoveationLevelFromCode(1, &level) && level == VROOpenXRFoveationLevel::LOW, "code 1 LOW");
        check(VROFoveationLevelFromCode(2, &level) && level == VROOpenXRFoveationLevel::MEDIUM, "code 2 MEDIUM");
        check(VROFoveationLevelFromCode(3, &level) && level == VROOpenXRFoveationLevel::HIGH, "code 3 HIGH");
        level = VROOpenXRFoveationLevel::MEDIUM;
        check(!VROFoveationLevelFromCode(4, &level) && level == VROOpenXRFoveationLevel::MEDIUM,
              "code 4 rejected, output untouched");
        check(!VROFoveationLevelFromCode(-1, &level), "code -1 rejected");
    }

    // render-quality defaults
    {
        VROOpenXRRenderQuality q;
        check(near(q.eyeBufferScale, 1.3f), "quality default scale");
        check(q.foveationLevel == VROOpenXRFoveationLevel::LOW, "quality default LOW");
        check(q.foveationDynamic, "quality default dynamic");
        check(q.displayRefreshRate.mode == VRODisplayRefreshRateMode::NONE,
              "quality default makes no refresh-rate request");
    }

    // display refresh rate: Quest 3 style list (the spec orders it low to high)
    {
        const float rates[] = { 72.0f, 80.0f, 90.0f, 120.0f };
        const size_t n = sizeof(rates) / sizeof(rates[0]);

        // unset: never request
        {
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest(), rates, n);
            check(c.outcome == VRODisplayRefreshRateOutcome::NO_REQUEST, "unset is NO_REQUEST");
            check(!c.shouldRequest(), "unset never calls xrRequestDisplayRefreshRateFB");
        }
        // highest: the max of the enumeration
        {
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::highest(), rates, n);
            check(c.outcome == VRODisplayRefreshRateOutcome::CHOSEN && c.shouldRequest(),
                  "highest is CHOSEN");
            check(near(c.rateHz, 120.0f), "highest picks 120");
        }
        // highest does not rely on the list being sorted
        {
            const float shuffled[] = { 90.0f, 120.0f, 72.0f };
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::highest(), shuffled, 3);
            check(near(c.rateHz, 120.0f), "highest on an unsorted list");
        }
        // exact match
        {
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::atMostHz(90.0f), rates, n);
            check(c.outcome == VRODisplayRefreshRateOutcome::CHOSEN, "exact is CHOSEN");
            check(near(c.rateHz, 90.0f), "exact match picks 90");
        }
        // a hair above a supported rate still matches it (float noise)
        {
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::atMostHz(89.999f), rates, n);
            check(near(c.rateHz, 90.0f), "89.999 matches 90");
        }
        // between rates: closest not above
        {
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::atMostHz(100.0f), rates, n);
            check(c.outcome == VRODisplayRefreshRateOutcome::CHOSEN, "between is CHOSEN");
            check(near(c.rateHz, 90.0f), "100 picks 90, never 120");
        }
        // above all rates: the highest
        {
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::atMostHz(240.0f), rates, n);
            check(near(c.rateHz, 120.0f), "240 picks 120");
        }
        // below all rates: the lowest, flagged so the caller logs it
        {
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::atMostHz(60.0f), rates, n);
            check(c.outcome == VRODisplayRefreshRateOutcome::BELOW_ALL_RATES, "60 is BELOW_ALL_RATES");
            check(c.shouldRequest(), "below all rates still requests");
            check(near(c.rateHz, 72.0f), "60 falls back to the lowest, 72");
        }
        // empty list: nothing to request
        {
            VRODisplayRefreshRateChoice a = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::highest(), nullptr, 0);
            VRODisplayRefreshRateChoice b = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::atMostHz(90.0f), rates, 0);
            check(a.outcome == VRODisplayRefreshRateOutcome::NO_SUPPORTED_RATES && !a.shouldRequest(),
                  "empty list, highest");
            check(b.outcome == VRODisplayRefreshRateOutcome::NO_SUPPORTED_RATES && !b.shouldRequest(),
                  "empty list, hz");
        }
        // junk entries in the enumeration are skipped
        {
            const float junk[] = { std::numeric_limits<float>::quiet_NaN(), 0.0f, -90.0f,
                                   std::numeric_limits<float>::infinity() };
            VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                VRODisplayRefreshRateRequest::highest(), junk, 4);
            check(c.outcome == VRODisplayRefreshRateOutcome::NO_SUPPORTED_RATES,
                  "a list of only non-positive or non-finite rates counts as empty");
        }
        // NaN, infinite, zero and negative requests are rejected, never requested
        {
            const float bad[] = { std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(),
                                  0.0f, -90.0f };
            for (float hz : bad) {
                VRODisplayRefreshRateChoice c = VROPickDisplayRefreshRate(
                    VRODisplayRefreshRateRequest::atMostHz(hz), rates, n);
                check(c.outcome == VRODisplayRefreshRateOutcome::INVALID_REQUEST, "bad hz rejected");
                check(!c.shouldRequest(), "bad hz never requested");
            }
        }
    }

    // display refresh rate JNI codes: 0 = none, 1 = highest, 2 = hz
    {
        VRODisplayRefreshRateRequest r;
        check(VRODisplayRefreshRateRequestFromCode(0, 0.0f, &r) &&
              r.mode == VRODisplayRefreshRateMode::NONE, "code 0 NONE");
        check(VRODisplayRefreshRateRequestFromCode(1, 0.0f, &r) &&
              r.mode == VRODisplayRefreshRateMode::HIGHEST, "code 1 HIGHEST");
        check(VRODisplayRefreshRateRequestFromCode(2, 90.0f, &r) &&
              r.mode == VRODisplayRefreshRateMode::HZ && near(r.hz, 90.0f), "code 2 HZ 90");
        r = VRODisplayRefreshRateRequest::highest();
        check(!VRODisplayRefreshRateRequestFromCode(3, 90.0f, &r) &&
              r.mode == VRODisplayRefreshRateMode::HIGHEST, "code 3 rejected, output untouched");
        check(!VRODisplayRefreshRateRequestFromCode(-1, 90.0f, &r), "code -1 rejected");
    }

    if (failures == 0) {
        std::printf("eye_buffer_scale_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "eye_buffer_scale_test: %d failure(s)\n", failures);
    return 1;
}
