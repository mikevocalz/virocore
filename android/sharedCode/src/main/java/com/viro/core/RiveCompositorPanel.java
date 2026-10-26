//
//  RiveCompositorPanel.java
//  ViroRenderer
//

package com.viro.core;

import android.util.Log;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/**
 * A Rive state machine shown as an OpenXR compositor layer (quad or cylinder)
 * on a {@link ViroViewOpenXR}, instead of being drawn into the scene.
 * <p>
 * The compositor composites the panel outside the eye buffers, so it is
 * sharper than a textured quad but is not depth-tested against the scene.
 * When the runtime has no free layer slot the panel is evicted: the
 * {@link Listener} hears {@link Presentation#EVICTED}, and the host shows
 * {@link #getFallbackTexture()} on a scene quad of the same pose and size,
 * with a {@link Material.BlendMode#PREMULTIPLY_ALPHA} material and a constant
 * lighting model. The fallback has the same pixels and rounded corners as the
 * layer. When a slot frees the panel returns to
 * {@link Presentation#COMPOSITOR} by itself and the host hides the quad.
 * <p>
 * Threads: create, pose, visibility and dispose are posted to the XR render
 * thread and take effect at the start of a later frame. View-model writes,
 * pointers and {@link #setActive} go to the Rive worker from any thread. All
 * callbacks run on the application (main) thread.
 * <p>
 * Lifetime: call {@link #dispose()} when the panel is no longer needed, and
 * dispose the fallback texture after that if the host took it. The fallback
 * keeps the Rive panel's native object alive until it is disposed. A session
 * teardown destroys the layer without a callback; the object then stops
 * receiving presentation changes and should be disposed.
 * <p>
 * Requires an arm64-v8a build with rive-runtime and an OpenXR session using
 * the GLES graphics binding. Check {@link #getCapabilities} first.
 */
public final class RiveCompositorPanel {

    private static final String TAG = "ViroRiveCompositorPanel";

    /** Layer geometry. */
    public enum Shape {
        /** Flat XrCompositionLayerQuad. Pose is the panel centre. */
        QUAD,
        /**
         * XrCompositionLayerCylinderKHR. Pose is the centre of the circle; the
         * arc sits radiusM in front of it. Needs
         * {@link Capabilities#supportsCylinder}.
         */
        CYLINDER
    }

    /** Where the panel's pixels were shown on the last composed frame. */
    public enum Presentation {
        /** Not composed yet: Rive has not published a frame, or no frame has run. */
        PENDING,
        /** Shown as a compositor layer. Hide the fallback quad. */
        COMPOSITOR,
        /** No layer slot. Show the fallback quad. */
        EVICTED
    }

    /** Why a panel was evicted. */
    public enum EvictionReason {
        NONE,
        /** No free layer slot this frame. */
        LAYER_BUDGET,
        /** The runtime rejected a frame for too many layers and the renderer lowered its cap. */
        LAYER_LIMIT_EXCEEDED
    }

    /** State of the Rive worker. Same order as VRORivePanelState. */
    public enum State {
        INITIALIZING,
        /** Unused for compositor panels, which have no window. */
        WAITING_FOR_SURFACE,
        ACTIVE,
        SUSPENDED,
        /** GPU or Rive failure. {@link #setActive}(true) rebuilds the context and re-imports the file. */
        LOST,
        DISPOSED
    }

    /** How the artboard fits the panel. Same order as VRORivePanelFit. */
    public enum Fit { FILL, CONTAIN, COVER, FIT_WIDTH, FIT_HEIGHT, NONE, SCALE_DOWN }

    /** Why {@link #create} failed. Same order as the native error codes. */
    public enum CreateError {
        SESSION_UNAVAILABLE,
        EMPTY_ID,
        MISSING_PRODUCER,
        DUPLICATE_ID,
        LAYER_BUDGET_EXCEEDED,
        INVALID_POSE,
        INVALID_DIMENSIONS,
        INVALID_PIXEL_SIZE,
        CYLINDER_UNAVAILABLE,
        INVALID_CYLINDER_RADIUS,
        INVALID_CYLINDER_ANGLE,
        SWAPCHAIN_FAILED,
        /** rive-runtime is not in this ABI, or the shared EGL context or finish pass failed. */
        PRODUCER_UNAVAILABLE,
        /** The .riv bytes were empty or larger than 64 MiB. */
        INVALID_RIVE_FILE
    }

    /** Facts from the current OpenXR session. */
    public static final class Capabilities {
        /** A session exists and accepts compositor panels. */
        public final boolean isSessionReady;
        /** Cylinder layers are enabled. */
        public final boolean supportsCylinder;
        /** Panel budget: the runtime's maxLayerCount minus projection and passthrough. */
        public final int maxPanels;
        /** rive-runtime is linked and the session has a shareable EGL context. */
        public final boolean supportsRive;

        Capabilities(boolean isSessionReady, boolean supportsCylinder, int maxPanels,
                     boolean supportsRive) {
            this.isSessionReady = isSessionReady;
            this.supportsCylinder = supportsCylinder;
            this.maxPanels = maxPanels;
            this.supportsRive = supportsRive;
        }
    }

    /** Receives {@link #getCapabilities} results on the main thread. */
    public interface CapabilitiesCallback {
        void onCapabilities(@NonNull Capabilities capabilities);
    }

    /** Receives the result of {@link #create} on the main thread. */
    public interface CreateCallback {
        void onCreated(@NonNull RiveCompositorPanel panel);

        /** The panel was not created; there is nothing to dispose. */
        void onFailed(@NonNull CreateError error, @NonNull String message);
    }

    /** Panel events, on the main thread. */
    public interface Listener {
        /**
         * The presentation changed. On {@link Presentation#EVICTED},
         * {@link RiveCompositorPanel#getFallbackTexture()} is set when the
         * renderer could build the fallback.
         */
        void onPresentationChanged(@NonNull RiveCompositorPanel panel,
                                   @NonNull Presentation presentation,
                                   @NonNull EvictionReason reason);

        /**
         * The Rive worker changed state, or a command failed without changing
         * it (bad .riv, unknown artboard or property): then detail is non-empty.
         */
        void onStatus(@NonNull RiveCompositorPanel panel, @NonNull State state,
                      @NonNull String detail);
    }

    /** Creation parameters. Sizes are metres, pixel sizes 16 to 4096. */
    public static final class Config {
        final String mId;
        final byte[] mRiveFile;
        Shape mShape = Shape.QUAD;
        float[] mPosition = {0f, 0f, -2f};
        float[] mOrientation = {0f, 0f, 0f, 1f};
        float mWidthM = 1.6f;
        float mHeightM = 0.9f;
        float mRadiusM = 0f;
        int mWidthPx = 1024;
        int mHeightPx = 576;
        boolean mVisible = true;
        float mCornerRadiusM = 0f;
        Fit mFit = Fit.CONTAIN;
        float mAlignmentX = 0f;
        float mAlignmentY = 0f;
        int mClearColor = 0;
        float mMaxFrameRateHz = 72f;
        String mArtboard = "";
        String mStateMachine = "";

        /**
         * @param id       Unique among this session's compositor panels.
         * @param riveFile The .riv bytes, copied on {@link #create}.
         */
        public Config(@NonNull String id, @NonNull byte[] riveFile) {
            mId = id;
            mRiveFile = riveFile;
        }

        public Config shape(@NonNull Shape shape) { mShape = shape; return this; }

        /** @param position xyz metres. @param orientation xyzw unit quaternion. */
        public Config pose(@NonNull float[] position, @NonNull float[] orientation) {
            mPosition = position.clone();
            mOrientation = orientation.clone();
            return this;
        }

        public Config size(float widthM, float heightM, int widthPx, int heightPx) {
            mWidthM = widthM;
            mHeightM = heightM;
            mWidthPx = widthPx;
            mHeightPx = heightPx;
            return this;
        }

        /** Cylinder radius in metres. Required for {@link Shape#CYLINDER}. */
        public Config cylinderRadius(float radiusM) { mRadiusM = radiusM; return this; }

        public Config visible(boolean visible) { mVisible = visible; return this; }

        /** Rounded-corner radius in metres, applied by the finish pass and the fallback. */
        public Config cornerRadius(float cornerRadiusM) { mCornerRadiusM = cornerRadiusM; return this; }

        public Config fit(@NonNull Fit fit) { mFit = fit; return this; }

        /** -1 left/top, 0 centre, 1 right/bottom. */
        public Config alignment(float x, float y) { mAlignmentX = x; mAlignmentY = y; return this; }

        /** Straight 0xAARRGGBB drawn under the artboard. Alpha 0 draws nothing. */
        public Config clearColor(int argb) { mClearColor = argb; return this; }

        /** Upper bound while animating. Settled content produces no frames. */
        public Config maxFrameRate(float hz) { mMaxFrameRateHz = hz; return this; }

        /** Empty selects the file's default. */
        public Config artboard(@NonNull String name) { mArtboard = name; return this; }

        /** Empty selects the artboard's default. */
        public Config stateMachine(@NonNull String name) { mStateMachine = name; return this; }
    }

    /** Pointer actions for {@link #pointer}. */
    public static final int POINTER_DOWN = 0;
    public static final int POINTER_MOVE = 1;
    public static final int POINTER_UP = 2;

    private final String mId;
    private final Listener mListener;
    private CreateCallback mCreateCallback;
    private long mNativeRef;
    private Presentation mPresentation = Presentation.PENDING;
    private State mState = State.INITIALIZING;
    private Texture mFallbackTexture;

    private RiveCompositorPanel(String id, Listener listener, CreateCallback callback) {
        mId = id;
        mListener = listener;
        mCreateCallback = callback;
    }

    /** Reads the session's compositor facts on the render thread. */
    public static void getCapabilities(@NonNull ViroViewOpenXR view,
                                       @NonNull CapabilitiesCallback callback) {
        nativeGetCapabilities(rendererRef(view), new CapabilitiesRelay(callback));
    }

    /**
     * Creates the panel on the render thread. The callback fires once.
     *
     * @throws IllegalArgumentException for a config Java can reject at once
     * @throws IllegalStateException    when the view has no OpenXR renderer yet
     */
    public static void create(@NonNull ViroViewOpenXR view, @NonNull Config config,
                              @NonNull Listener listener, @NonNull CreateCallback callback) {
        if (config.mPosition.length != 3 || config.mOrientation.length != 4) {
            throw new IllegalArgumentException("pose needs 3 position and 4 orientation values");
        }
        if (!(config.mCornerRadiusM >= 0f) || Float.isInfinite(config.mCornerRadiusM)) {
            throw new IllegalArgumentException("cornerRadius must be finite and >= 0, got "
                    + config.mCornerRadiusM);
        }
        long rendererRef = rendererRef(view);
        if (rendererRef == 0) {
            throw new IllegalStateException("ViroViewOpenXR has no renderer yet; attach it first");
        }
        RiveCompositorPanel panel = new RiveCompositorPanel(config.mId, listener, callback);
        synchronized (panel) {
            panel.mNativeRef = panel.nativeCreate(rendererRef, config.mId, config.mShape.ordinal(),
                    config.mPosition, config.mOrientation, config.mWidthM, config.mHeightM,
                    config.mRadiusM, config.mWidthPx, config.mHeightPx, config.mVisible,
                    config.mCornerRadiusM, config.mFit.ordinal(), config.mAlignmentX,
                    config.mAlignmentY, config.mClearColor, config.mMaxFrameRateHz,
                    config.mRiveFile, config.mArtboard, config.mStateMachine);
        }
        if (panel.mNativeRef == 0) {
            throw new IllegalStateException("the view's renderer is not an OpenXR renderer");
        }
    }

    @NonNull
    public String getId() { return mId; }

    /** The last presentation delivered to the listener. Main thread. */
    @NonNull
    public Presentation getPresentation() { return mPresentation; }

    /** The last Rive state delivered to the listener. Main thread. */
    @NonNull
    public State getState() { return mState; }

    /**
     * The scene fallback texture, set on the first eviction and kept until
     * {@link #dispose()}. The same texture serves every later eviction and
     * keeps updating while the panel is evicted. Main thread.
     */
    @Nullable
    public Texture getFallbackTexture() { return mFallbackTexture; }

    /** Moves the layer. Takes effect on a later frame. */
    public synchronized void setPose(@NonNull float[] position, @NonNull float[] orientation) {
        if (mNativeRef == 0) return;
        nativeSetPose(mNativeRef, position, orientation);
    }

    /** Shows or hides the layer. A hidden panel holds no slot and is not evicted. */
    public synchronized void setVisible(boolean visible) {
        if (mNativeRef == 0) return;
        nativeSetVisible(mNativeRef, visible);
    }

    /** View-model number write. False if the panel refused to queue it. */
    public synchronized boolean setNumber(@NonNull String path, float value) {
        return mNativeRef != 0 && nativeSetNumber(mNativeRef, path, value);
    }

    public synchronized boolean setBoolean(@NonNull String path, boolean value) {
        return mNativeRef != 0 && nativeSetBoolean(mNativeRef, path, value);
    }

    public synchronized boolean setString(@NonNull String path, @NonNull String value) {
        return mNativeRef != 0 && nativeSetString(mNativeRef, path, value);
    }

    public synchronized boolean fireTrigger(@NonNull String path) {
        return mNativeRef != 0 && nativeFireTrigger(mNativeRef, path);
    }

    /**
     * Pointer input in panel pixels, origin top-left. A press that starts in a
     * transparent corner is dropped. Use a distinct pointerId per hand,
     * controller and pen.
     *
     * @param action {@link #POINTER_DOWN}, {@link #POINTER_MOVE} or {@link #POINTER_UP}
     */
    public synchronized boolean pointer(int action, float xPx, float yPx, int pointerId) {
        return mNativeRef != 0 && nativePointer(mNativeRef, action, xPx, yPx, pointerId);
    }

    /** Pauses or resumes Rive. After {@link State#LOST}, true rebuilds it. */
    public synchronized boolean setActive(boolean active) {
        return mNativeRef != 0 && nativeSetActive(mNativeRef, active);
    }

    /**
     * Removes the layer and frees the Rive worker. Idempotent. The fallback
     * texture, if taken, stays valid until the caller disposes it.
     */
    public synchronized void dispose() {
        if (mNativeRef == 0) return;
        nativeDispose(mNativeRef);
        mNativeRef = 0;
    }

    private static long rendererRef(ViroViewOpenXR view) {
        Renderer renderer = view.mNativeRenderer;
        return renderer == null ? 0 : renderer.mNativeRef;
    }

    // ── Called from native on the main thread ───────────────────────────────

    @Keep
    void onNativeCreated() {
        CreateCallback callback = mCreateCallback;
        mCreateCallback = null;
        if (callback == null) return;
        try {
            callback.onCreated(this);
        } catch (RuntimeException e) {
            Log.e(TAG, "onCreated threw for " + mId, e);
        }
    }

    @Keep
    void onNativeCreateFailed(int code, String message) {
        dispose();
        CreateCallback callback = mCreateCallback;
        mCreateCallback = null;
        if (callback == null) return;
        CreateError[] errors = CreateError.values();
        CreateError error = code >= 0 && code < errors.length
                ? errors[code] : CreateError.PRODUCER_UNAVAILABLE;
        try {
            callback.onFailed(error, message != null ? message : error.name());
        } catch (RuntimeException e) {
            Log.e(TAG, "onFailed threw for " + mId, e);
        }
    }

    @Keep
    void onNativePresentation(int presentation, int reason, long fallbackTextureRef) {
        if (fallbackTextureRef != 0) {
            if (mFallbackTexture == null) {
                mFallbackTexture = new Texture(fallbackTextureRef);
            } else {
                // Native hands the texture out once; a second ref is a bug, not a leak to keep.
                new Texture(fallbackTextureRef).dispose();
            }
        }
        Presentation[] presentations = Presentation.values();
        EvictionReason[] reasons = EvictionReason.values();
        if (presentation < 0 || presentation >= presentations.length) return;
        mPresentation = presentations[presentation];
        EvictionReason why = reason >= 0 && reason < reasons.length
                ? reasons[reason] : EvictionReason.NONE;
        try {
            mListener.onPresentationChanged(this, mPresentation, why);
        } catch (RuntimeException e) {
            Log.e(TAG, "onPresentationChanged threw for " + mId, e);
        }
    }

    @Keep
    void onNativeStatus(int state, String detail) {
        State[] states = State.values();
        if (state < 0 || state >= states.length) return;
        mState = states[state];
        try {
            mListener.onStatus(this, mState, detail != null ? detail : "");
        } catch (RuntimeException e) {
            Log.e(TAG, "onStatus threw for " + mId, e);
        }
    }

    @Keep
    static final class CapabilitiesRelay {
        private final CapabilitiesCallback mCallback;

        CapabilitiesRelay(CapabilitiesCallback callback) { mCallback = callback; }

        @Keep
        void onNativeCapabilities(boolean sessionReady, boolean cylinder, int maxPanels,
                                  boolean rive) {
            try {
                mCallback.onCapabilities(new Capabilities(sessionReady, cylinder, maxPanels, rive));
            } catch (RuntimeException e) {
                Log.e(TAG, "onCapabilities threw", e);
            }
        }
    }

    private native long nativeCreate(long rendererRef, String id, int shape, float[] position,
                                     float[] orientation, float widthM, float heightM,
                                     float radiusM, int widthPx, int heightPx, boolean visible,
                                     float cornerRadiusM, int fit, float alignmentX,
                                     float alignmentY, int clearColor, float maxFrameRateHz,
                                     byte[] riveFile, String artboard, String stateMachine);
    private native void nativeDispose(long ref);
    private native void nativeSetPose(long ref, float[] position, float[] orientation);
    private native void nativeSetVisible(long ref, boolean visible);
    private native boolean nativeSetNumber(long ref, String path, float value);
    private native boolean nativeSetBoolean(long ref, String path, boolean value);
    private native boolean nativeSetString(long ref, String path, String value);
    private native boolean nativeFireTrigger(long ref, String path);
    private native boolean nativePointer(long ref, int action, float xPx, float yPx, int pointerId);
    private native boolean nativeSetActive(long ref, boolean active);
    private static native void nativeGetCapabilities(long rendererRef, CapabilitiesRelay callback);
}
