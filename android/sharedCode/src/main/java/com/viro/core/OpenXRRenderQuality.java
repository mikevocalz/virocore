//
//  OpenXRRenderQuality.java
//  ViroCore
//

package com.viro.core;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/**
 * Startup render quality for {@link ViroViewOpenXR}: the eye-buffer scale, the
 * fixed-foveation level the session starts with, and an optional display
 * refresh rate request ({@link OpenXRDisplayRefreshRate}).
 *
 * <p>The eye-buffer scale sizes the per-eye swapchains, which are created once
 * when the OpenXR session starts and live until the view is disposed. It is
 * startup-only: pass it through {@link ViroViewOpenXR#setRenderQuality} before
 * the view attaches, or through the AndroidManifest meta-data that
 * {@link OpenXRRenderQualityManifest} reads. The foveation level here is only
 * the starting level; {@link ViroViewOpenXR#setFoveationLevel(int, boolean)}
 * changes it while the session runs.
 *
 * <p>The scale is a per-axis multiplier on the runtime's recommended eye-buffer
 * size. The renderer clamps it per axis to
 * {@code [1.0, maxImageRect / recommendedImageRect]}: a value below 1.0 renders
 * at the recommended size, and a value above the runtime's headroom renders at
 * the runtime maximum on that axis. Every eye logs the requested, applied and
 * final sizes (see docs/API_RENDER_QUALITY.md).
 *
 * <p>The display refresh rate is null by default, which makes no request and
 * leaves the runtime at its own rate.
 *
 * <p>Instances are immutable.
 */
public final class OpenXRRenderQuality {

    /** Eye-buffer scale used when nothing is configured. */
    public static final float DEFAULT_EYE_BUFFER_SCALE = 1.3f;

    /** Foveation level used when nothing is configured. */
    @NonNull
    public static final OpenXRFoveationLevel DEFAULT_FOVEATION_LEVEL = OpenXRFoveationLevel.LOW;

    /** Whether foveation is dynamic when nothing is configured. */
    public static final boolean DEFAULT_FOVEATION_DYNAMIC = true;

    /** 1.3x eye buffers, LOW dynamic foveation. */
    @NonNull
    public static final OpenXRRenderQuality DEFAULT = new OpenXRRenderQuality(
            DEFAULT_EYE_BUFFER_SCALE, DEFAULT_FOVEATION_LEVEL, DEFAULT_FOVEATION_DYNAMIC);

    private final float mEyeBufferScale;
    @NonNull
    private final OpenXRFoveationLevel mFoveationLevel;
    private final boolean mFoveationDynamic;
    @Nullable
    private final OpenXRDisplayRefreshRate mDisplayRefreshRate;

    /**
     * Render quality with no display refresh rate request.
     *
     * @param eyeBufferScale   Per-axis multiplier on the recommended eye-buffer
     *                         size. Values below 1.0 are accepted and clamp to
     *                         1.0 in the renderer.
     * @param foveationLevel   Fixed-foveation level applied once the swapchains exist.
     * @param foveationDynamic Let the runtime lower the level when the GPU has headroom.
     * @throws IllegalArgumentException when {@code eyeBufferScale} is NaN or infinite.
     * @throws NullPointerException     when {@code foveationLevel} is null.
     */
    public OpenXRRenderQuality(float eyeBufferScale,
                               @NonNull OpenXRFoveationLevel foveationLevel,
                               boolean foveationDynamic) {
        this(eyeBufferScale, foveationLevel, foveationDynamic, null);
    }

    /**
     * @param eyeBufferScale     Per-axis multiplier on the recommended eye-buffer
     *                           size. Values below 1.0 are accepted and clamp to
     *                           1.0 in the renderer.
     * @param foveationLevel     Fixed-foveation level applied once the swapchains exist.
     * @param foveationDynamic   Let the runtime lower the level when the GPU has headroom.
     * @param displayRefreshRate Rate to request each time the session begins, or
     *                           null to make no request and keep the runtime's rate.
     * @throws IllegalArgumentException when {@code eyeBufferScale} is NaN or infinite.
     * @throws NullPointerException     when {@code foveationLevel} is null.
     */
    public OpenXRRenderQuality(float eyeBufferScale,
                               @NonNull OpenXRFoveationLevel foveationLevel,
                               boolean foveationDynamic,
                               @Nullable OpenXRDisplayRefreshRate displayRefreshRate) {
        if (Float.isNaN(eyeBufferScale) || Float.isInfinite(eyeBufferScale)) {
            throw new IllegalArgumentException(
                    "eyeBufferScale must be finite, got " + eyeBufferScale
                            + "; use " + DEFAULT_EYE_BUFFER_SCALE + " for the default");
        }
        if (foveationLevel == null) {
            throw new NullPointerException("foveationLevel");
        }
        mEyeBufferScale = eyeBufferScale;
        mFoveationLevel = foveationLevel;
        mFoveationDynamic = foveationDynamic;
        mDisplayRefreshRate = displayRefreshRate;
    }

    /** The requested eye-buffer scale, before the renderer's per-axis clamp. */
    public float getEyeBufferScale() {
        return mEyeBufferScale;
    }

    /** The foveation level the session starts with. */
    @NonNull
    public OpenXRFoveationLevel getFoveationLevel() {
        return mFoveationLevel;
    }

    /** Whether the starting foveation is dynamic. */
    public boolean isFoveationDynamic() {
        return mFoveationDynamic;
    }

    /**
     * The display refresh rate to request, or null when no request is made and
     * the runtime keeps its own rate.
     */
    @Nullable
    public OpenXRDisplayRefreshRate getDisplayRefreshRate() {
        return mDisplayRefreshRate;
    }

    /** JNI mode code for the refresh rate: 0 none, 1 highest, 2 at most Hz. */
    int getDisplayRefreshRateModeCode() {
        return mDisplayRefreshRate == null ? 0 : mDisplayRefreshRate.getModeCode();
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof OpenXRRenderQuality)) {
            return false;
        }
        OpenXRRenderQuality that = (OpenXRRenderQuality) other;
        return Float.compare(mEyeBufferScale, that.mEyeBufferScale) == 0
                && mFoveationLevel == that.mFoveationLevel
                && mFoveationDynamic == that.mFoveationDynamic
                && (mDisplayRefreshRate == null
                        ? that.mDisplayRefreshRate == null
                        : mDisplayRefreshRate.equals(that.mDisplayRefreshRate));
    }

    @Override
    public int hashCode() {
        int result = Float.floatToIntBits(mEyeBufferScale);
        result = 31 * result + mFoveationLevel.hashCode();
        result = 31 * result + (mFoveationDynamic ? 1 : 0);
        result = 31 * result + (mDisplayRefreshRate == null ? 0 : mDisplayRefreshRate.hashCode());
        return result;
    }

    @Override
    @NonNull
    public String toString() {
        return "OpenXRRenderQuality{eyeBufferScale=" + mEyeBufferScale
                + ", foveationLevel=" + mFoveationLevel
                + ", foveationDynamic=" + mFoveationDynamic
                + ", displayRefreshRate=" + (mDisplayRefreshRate == null ? "runtime default" : mDisplayRefreshRate)
                + "}";
    }
}
