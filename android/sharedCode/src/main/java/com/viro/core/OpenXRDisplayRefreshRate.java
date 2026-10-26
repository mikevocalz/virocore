//
//  OpenXRDisplayRefreshRate.java
//  ViroCore
//

package com.viro.core;

import androidx.annotation.NonNull;

/**
 * A display refresh rate for {@link ViroViewOpenXR} to request through
 * XR_FB_display_refresh_rate, set with
 * {@link OpenXRRenderQuality#OpenXRRenderQuality(float, OpenXRFoveationLevel, boolean, OpenXRDisplayRefreshRate)}.
 *
 * <p>Leaving it out of {@link OpenXRRenderQuality} (a null rate) is the default:
 * the renderer makes no request and the runtime keeps its own rate.
 *
 * <p>A request picks from the rates the runtime enumerates:
 * <ul>
 *   <li>{@link #HIGHEST}: the highest one.</li>
 *   <li>{@link #atMostHz(float)}: the closest one at or below the given rate. If
 *       every supported rate is above it, the lowest supported rate is used and
 *       the renderer logs that.</li>
 * </ul>
 * The renderer requests the rate each time the session begins, and logs
 * {@code display refresh rate: requested X, chosen Y, current Z} with the
 * result. The runtime treats the request as a hint, and on runtimes without the
 * extension nothing is requested. Neither case stops the session.
 *
 * <p>Instances are immutable.
 */
public final class OpenXRDisplayRefreshRate {

    // JNI codes; keep in sync with VRODisplayRefreshRateMode.
    static final int MODE_HIGHEST = 1;
    static final int MODE_HZ = 2;

    /** Request the highest rate the runtime supports. */
    @NonNull
    public static final OpenXRDisplayRefreshRate HIGHEST = new OpenXRDisplayRefreshRate(MODE_HIGHEST, 0.0f);

    private final int mMode;
    private final float mHz;

    private OpenXRDisplayRefreshRate(int mode, float hz) {
        mMode = mode;
        mHz = hz;
    }

    /**
     * Request the closest supported rate at or below {@code hz}.
     *
     * @param hz Refresh rate in Hz.
     * @throws IllegalArgumentException when {@code hz} is NaN, infinite, zero or negative.
     */
    @NonNull
    public static OpenXRDisplayRefreshRate atMostHz(float hz) {
        if (Float.isNaN(hz) || Float.isInfinite(hz) || hz <= 0.0f) {
            throw new IllegalArgumentException(
                    "display refresh rate must be a positive finite number of Hz, got " + hz
                            + "; use OpenXRDisplayRefreshRate.HIGHEST for the highest supported rate");
        }
        return new OpenXRDisplayRefreshRate(MODE_HZ, hz);
    }

    /** True for {@link #HIGHEST}. */
    public boolean isHighest() {
        return mMode == MODE_HIGHEST;
    }

    /** The requested ceiling in Hz for {@link #atMostHz(float)}; 0 for {@link #HIGHEST}. */
    public float getHz() {
        return mHz;
    }

    int getModeCode() {
        return mMode;
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof OpenXRDisplayRefreshRate)) {
            return false;
        }
        OpenXRDisplayRefreshRate that = (OpenXRDisplayRefreshRate) other;
        return mMode == that.mMode && Float.compare(mHz, that.mHz) == 0;
    }

    @Override
    public int hashCode() {
        return 31 * mMode + Float.floatToIntBits(mHz);
    }

    @Override
    @NonNull
    public String toString() {
        return isHighest() ? "highest" : (mHz + " Hz");
    }
}
