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
    }

    if (failures == 0) {
        std::printf("eye_buffer_scale_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "eye_buffer_scale_test: %d failure(s)\n", failures);
    return 1;
}
