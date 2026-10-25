//
//  OpenXRFoveationLevel.java
//  ViroCore
//

package com.viro.core;

/**
 * Fixed-foveation level for the OpenXR renderer (XR_FB_foveation). Higher
 * levels render the periphery of each eye at lower resolution and save more GPU
 * time. A runtime without XR_FB_foveation renders unfoveated at any level.
 *
 * @see OpenXRRenderQuality#getFoveationLevel()
 * @see ViroViewOpenXR#setFoveationLevel(int, boolean)
 */
public enum OpenXRFoveationLevel {
    /** No foveation. */
    OFF(0),
    /** Light foveation. The default: panel text in the periphery stays legible. */
    LOW(1),
    /** Medium foveation. */
    MEDIUM(2),
    /** Strongest foveation. */
    HIGH(3);

    private final int mCode;

    OpenXRFoveationLevel(int code) {
        mCode = code;
    }

    /**
     * The integer code {@link ViroViewOpenXR#setFoveationLevel(int, boolean)}
     * accepts and the native renderer receives.
     */
    public int getCode() {
        return mCode;
    }
}
