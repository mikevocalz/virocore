//
//  OpenXRRenderQualityManifest.java
//  ViroCore
//

package com.viro.core;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.Locale;

/**
 * Reads the default {@link OpenXRRenderQuality} from the application's
 * AndroidManifest {@code <meta-data>}:
 *
 * <pre>{@code
 * <meta-data android:name="com.viro.openxr.eyeBufferScale"   android:value="1.5"/>
 * <meta-data android:name="com.viro.openxr.foveationLevel"   android:value="medium"/>
 * <meta-data android:name="com.viro.openxr.foveationDynamic" android:value="false"/>
 * <meta-data android:name="com.viro.openxr.displayRefreshRate" android:value="highest"/>
 * }</pre>
 *
 * <p>{@code displayRefreshRate} is {@code highest} or a number of Hz (see
 * {@link OpenXRDisplayRefreshRate}). Without it the renderer makes no refresh
 * rate request.
 *
 * <p>The Viro Expo config plugin writes these from
 * {@code android.openxr} and validates them at prebuild. A key that is absent
 * keeps its default. A key with an unreadable value also keeps its default and
 * logs a warning, so a hand-edited manifest cannot stop the renderer starting.
 */
public final class OpenXRRenderQualityManifest {

    /** Float, per-axis eye-buffer scale. */
    public static final String KEY_EYE_BUFFER_SCALE = "com.viro.openxr.eyeBufferScale";
    /** String: {@code off}, {@code low}, {@code medium} or {@code high}. */
    public static final String KEY_FOVEATION_LEVEL = "com.viro.openxr.foveationLevel";
    /** Boolean. */
    public static final String KEY_FOVEATION_DYNAMIC = "com.viro.openxr.foveationDynamic";
    /** String {@code highest}, or a positive number of Hz. */
    public static final String KEY_DISPLAY_REFRESH_RATE = "com.viro.openxr.displayRefreshRate";

    private static final String TAG = "VRORendererOpenXR";

    private OpenXRRenderQualityManifest() {
    }

    /**
     * The render quality the application's manifest declares, with
     * {@link OpenXRRenderQuality#DEFAULT} filling every key it leaves out.
     */
    @NonNull
    public static OpenXRRenderQuality read(@NonNull Context context) {
        return fromMetaData(applicationMetaData(context));
    }

    /**
     * Same as {@link #read(Context)}, from an already-loaded meta-data bundle.
     * A null bundle (no meta-data at all) yields {@link OpenXRRenderQuality#DEFAULT}.
     */
    @NonNull
    public static OpenXRRenderQuality fromMetaData(@Nullable Bundle metaData) {
        if (metaData == null) {
            return OpenXRRenderQuality.DEFAULT;
        }
        float scale = readScale(metaData.get(KEY_EYE_BUFFER_SCALE));
        OpenXRFoveationLevel level = readLevel(metaData.get(KEY_FOVEATION_LEVEL));
        boolean dynamic = readDynamic(metaData.get(KEY_FOVEATION_DYNAMIC));
        OpenXRDisplayRefreshRate rate = readRefreshRate(metaData.get(KEY_DISPLAY_REFRESH_RATE));
        return new OpenXRRenderQuality(scale, level, dynamic, rate);
    }

    @Nullable
    private static Bundle applicationMetaData(@NonNull Context context) {
        try {
            ApplicationInfo info = context.getPackageManager().getApplicationInfo(
                    context.getPackageName(), PackageManager.GET_META_DATA);
            return info.metaData;
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "Cannot read manifest meta-data; using default render quality", e);
            return null;
        }
    }

    // aapt stores android:value="1.5" as Float and "2" as Integer.
    private static float readScale(@Nullable Object raw) {
        if (raw == null) {
            return OpenXRRenderQuality.DEFAULT_EYE_BUFFER_SCALE;
        }
        float value = Float.NaN;
        if (raw instanceof Number) {
            value = ((Number) raw).floatValue();
        } else if (raw instanceof String) {
            try {
                value = Float.parseFloat(((String) raw).trim());
            } catch (NumberFormatException ignored) {
                // falls through to the warning below
            }
        }
        if (Float.isNaN(value) || Float.isInfinite(value)) {
            warnIgnored(KEY_EYE_BUFFER_SCALE, raw, "a finite number");
            return OpenXRRenderQuality.DEFAULT_EYE_BUFFER_SCALE;
        }
        return value;
    }

    private static OpenXRFoveationLevel readLevel(@Nullable Object raw) {
        if (raw == null) {
            return OpenXRRenderQuality.DEFAULT_FOVEATION_LEVEL;
        }
        if (raw instanceof String) {
            String name = ((String) raw).trim().toUpperCase(Locale.ROOT);
            for (OpenXRFoveationLevel level : OpenXRFoveationLevel.values()) {
                if (level.name().equals(name)) {
                    return level;
                }
            }
        }
        warnIgnored(KEY_FOVEATION_LEVEL, raw, "off, low, medium or high");
        return OpenXRRenderQuality.DEFAULT_FOVEATION_LEVEL;
    }

    // aapt stores android:value="true" as Boolean.
    private static boolean readDynamic(@Nullable Object raw) {
        if (raw == null) {
            return OpenXRRenderQuality.DEFAULT_FOVEATION_DYNAMIC;
        }
        if (raw instanceof Boolean) {
            return (Boolean) raw;
        }
        if (raw instanceof String) {
            String value = ((String) raw).trim();
            if (value.equalsIgnoreCase("true")) {
                return true;
            }
            if (value.equalsIgnoreCase("false")) {
                return false;
            }
        }
        warnIgnored(KEY_FOVEATION_DYNAMIC, raw, "true or false");
        return OpenXRRenderQuality.DEFAULT_FOVEATION_DYNAMIC;
    }

    // aapt stores "highest" as String, "120" as Integer and "72.5" as Float.
    @Nullable
    private static OpenXRDisplayRefreshRate readRefreshRate(@Nullable Object raw) {
        if (raw == null) {
            return null;
        }
        float hz = Float.NaN;
        if (raw instanceof Number) {
            hz = ((Number) raw).floatValue();
        } else if (raw instanceof String) {
            String value = ((String) raw).trim();
            if (value.equalsIgnoreCase("highest")) {
                return OpenXRDisplayRefreshRate.HIGHEST;
            }
            try {
                hz = Float.parseFloat(value);
            } catch (NumberFormatException ignored) {
                // falls through to the warning below
            }
        }
        if (Float.isNaN(hz) || Float.isInfinite(hz) || hz <= 0.0f) {
            warnIgnored(KEY_DISPLAY_REFRESH_RATE, raw, "highest or a positive number of Hz");
            return null;
        }
        return OpenXRDisplayRefreshRate.atMostHz(hz);
    }

    private static void warnIgnored(String key, Object raw, String expected) {
        Log.w(TAG, "Ignoring <meta-data " + key + "=\"" + raw + "\">: expected "
                + expected + "; using the default");
    }
}
