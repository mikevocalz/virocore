//
//  Copyright (c) 2017-present, ViroMedia, Inc.
//  All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

/*
 * This class is a convenience wrapper around:
 *
 * Cpp JNI wrapper      : VRORenderer_JNI.cpp
 * Cpp Object           : VROSceneRendererGVR.cpp
 */
package com.viro.core;

import android.app.Activity;
import android.content.Context;
import android.content.res.AssetManager;
import android.view.Surface;
import android.os.Handler;
import android.os.Looper;
import androidx.annotation.Keep;

import com.viro.core.internal.PlatformUtil;

import java.lang.ref.WeakReference;

/**
 * @hide
 */
public class Renderer {

    protected long mNativeRef;
    private CameraListener mCameraListener;
    private FrameListener mFrameListener;
    protected NativeFrameListenerImpl mNativeFrameListener;

    protected static class NativeFrameListenerImpl extends NativeFrameListener {
        WeakReference<Renderer> mWeakRenderer;

        public NativeFrameListenerImpl(Renderer renderer) {
            mWeakRenderer = new WeakReference<Renderer>(renderer);
            mNativeRef = renderer.nativeCreateFrameListener(renderer.mNativeRef);
        }

        @Override
        public void destroy() {
            if (mNativeRef != 0) {
                Renderer renderer = mWeakRenderer.get();
                if (renderer != null) {
                    renderer.nativeDestroyFrameListener(mNativeRef);
                }
            }
            mNativeRef = 0;
        }
    }

    protected Renderer() {

    }

    /* ----------     Scene view only methods    ---------- */

    public Renderer(ClassLoader appClassLoader, Context context, ViroViewScene view, AssetManager assets,
                    PlatformUtil platformUtil, RendererConfiguration config) {
        mNativeRef = nativeCreateRendererSceneView(appClassLoader, context, view, assets, platformUtil,
                config.isShadowsEnabled(), config.isHDREnabled(), config.isPBREnabled(), config.isBloomEnabled());
    }

    /* ----------     GVR only methods    ---------- */

    public Renderer(ClassLoader appClassLoader, Context context,
                    AssetManager assets, PlatformUtil platformUtil, long nativeGvrContext,
                    RendererConfiguration config) {
        mNativeRef = nativeCreateRendererGVR(appClassLoader, context, assets, platformUtil, nativeGvrContext,
                config.isShadowsEnabled(), config.isHDREnabled(), config.isPBREnabled(), config.isBloomEnabled());
    }

    public void drawFrame() {
        if (mNativeRef != 0) {
            nativeDrawFrame(mNativeRef);
        }
    }
    public void setVRModeEnabled(boolean enabled) { nativeSetVRModeEnabled(mNativeRef, enabled); }

    /* ----------     OVR only methods (deprecated 2.57.3 — VrApi removed, see viro#491)    ---------- */
    @Deprecated
    public Renderer(ClassLoader appClassLoader, Context context,
                    ViroViewOVR view, Activity activity, AssetManager assets, PlatformUtil platformUtil,
                    RendererConfiguration config) {
        mNativeRef = nativeCreateRendererOVR(appClassLoader, context, view, activity, assets, platformUtil,
                config.isShadowsEnabled(), config.isHDREnabled(), config.isPBREnabled(), config.isBloomEnabled());
    }

    public void onSurfaceDestroyed(Surface surface) { nativeOnSurfaceDestroyed(mNativeRef); }

    /* ----------     OpenXR (Meta Quest) methods    ---------- */
    public Renderer(ClassLoader appClassLoader, Context context,
                    ViroViewOpenXR view, Activity activity, AssetManager assets, PlatformUtil platformUtil,
                    RendererConfiguration config) {
        mNativeRef = nativeCreateRendererOpenXR(appClassLoader, context, view, activity, assets, platformUtil,
                config.isShadowsEnabled(), config.isHDREnabled(), config.isPBREnabled(), config.isBloomEnabled());
    }

    public void recenterTracking() { nativeRecenterTracking(mNativeRef); }

    /** Explicit provenance: UNKNOWN Camera2 timestamps must not be cast into this domain. */
    public enum CaptureClockDomain {
        UNKNOWN(0), EXTERNALLY_VERIFIED_CLOCK_MONOTONIC(1);
        final int code;
        CaptureClockDomain(int value) { code = value; }
    }
    public enum CaptureHeadPoseStatus {
        VALID, NOT_READY, UNSUPPORTED, INVALID_CLOCK, INVALID_TIMESTAMP, SPACE_CHANGED,
        CONVERSION_FAILED, LOCATION_FAILED, UNTRACKED, TIMEOUT, BUSY, DESTROYED
    }
    /** Head VIEW in the renderer's app space, metres and quaternion xyzw. Never a pen pose. */
    public static final class CaptureHeadPoseResult {
        public final CaptureHeadPoseStatus status;
        public final long captureTimestampNanos, captureXrTime, spaceGeneration, locationFlags;
        public final int xrResult;
        public final CaptureClockDomain clockDomain;
        /** Null unless VALID. xyz followed by quaternion xyzw; no zero-pose fallback. */
        public final float[] pose;
        private CaptureHeadPoseResult(int code, long stamp, long xrTime, long generation,
                                      long flags, int result, CaptureClockDomain domain, float[] values) {
            status = CaptureHeadPoseStatus.values()[code];
            captureTimestampNanos = stamp; captureXrTime = xrTime; spaceGeneration = generation;
            locationFlags = flags; xrResult = result; clockDomain = domain;
            pose = status == CaptureHeadPoseStatus.VALID && values != null ? values.clone() : null;
        }
    }
    public interface CaptureHeadPoseCallback { void onResult(CaptureHeadPoseResult result); }
    private final Object mCapturePoseLock = new Object();
    private final Handler mCapturePoseHandler = new Handler(Looper.getMainLooper());
    private long mCapturePoseSequence, mCapturePosePending;
    private CaptureHeadPoseCallback mCapturePoseCallback;
    private CaptureClockDomain mCapturePoseDomain;
    private long mCapturePoseTimestamp;
    private boolean mCapturePoseDestroyed;
    private Runnable mCapturePoseTimeout;

    /**
     * One bounded asynchronous head-pose query; callback on Android main thread.
     * Latch getCaptureHeadPoseSpaceGeneration() BEFORE acquiring camera frames; pass that value
     * with each frame. A zero/stale generation never yields a valid pose.
     * Requires an externally verified CLOCK_MONOTONIC timestamp within500ms.
     * Timeout is1s. A timed-out native task keeps its one queue slot until drained.
     * Results are invalid after spaceGeneration changes (recenter/session/origin).
     */
    public long getCaptureHeadPoseSpaceGeneration() {
        synchronized (mCapturePoseLock) {
            return mCapturePoseDestroyed || mNativeRef == 0 ? 0 : nativeCapturePoseGeneration(mNativeRef);
        }
    }
    public void requestCaptureHeadPose(long timestampNanos, long expectedSpaceGeneration, CaptureClockDomain domain,
                                       CaptureHeadPoseCallback callback) {
        if (domain == null || callback == null) throw new IllegalArgumentException("Domain and callback required");
        final long id;
        final int rejected;
        synchronized (mCapturePoseLock) {
            rejected = mCapturePoseDestroyed ? 11 : mNativeRef == 0 ? 1 : mCapturePosePending != 0 ? 10 : -1;
            id = ++mCapturePoseSequence;
            if (rejected < 0) {
                mCapturePosePending = id; mCapturePoseCallback = callback;
                mCapturePoseDomain = domain; mCapturePoseTimestamp = timestampNanos;
                mCapturePoseTimeout = () -> finishCaptureHeadPose(id, 9, 0, 0, 0, 0, null, false);
                mCapturePoseHandler.postDelayed(mCapturePoseTimeout, 1000);
                // Queue admission and native handle use stay serialized against destroy().
                nativeRequestCaptureHeadPose(mNativeRef, id, timestampNanos, domain.code, expectedSpaceGeneration);
            }
        }
        if (rejected >= 0) mCapturePoseHandler.post(() -> callback.onResult(
            new CaptureHeadPoseResult(rejected, timestampNanos, 0, 0, 0, 0, domain, null)));
    }
    // JNI entry; native completion is posted to the application/main thread.
    @Keep
    private void onCaptureHeadPoseResult(long id, int status, long xrTime, long generation,
                                        long flags, int xrResult, float[] pose) {
        finishCaptureHeadPose(id, status, xrTime, generation, flags, xrResult, pose, true);
    }
    private void finishCaptureHeadPose(long id, int status, long xrTime, long generation,
                                       long flags, int xrResult, float[] pose, boolean nativeFinished) {
        CaptureHeadPoseCallback callback;
        CaptureHeadPoseResult result;
        synchronized (mCapturePoseLock) {
            if (id != mCapturePosePending) return;
            if (mCapturePoseDestroyed) status = 11;
            callback = mCapturePoseCallback; mCapturePoseCallback = null;
            mCapturePoseHandler.removeCallbacks(mCapturePoseTimeout);
            result = new CaptureHeadPoseResult(status, mCapturePoseTimestamp, xrTime, generation,
                                               flags, xrResult, mCapturePoseDomain, pose);
            if (nativeFinished) mCapturePosePending = 0;
        }
        if (callback != null) callback.onResult(result);
    }


    /** Enable or disable XR_FB_passthrough mixed-reality mode (Quest 3 / Quest Pro only). */
    public void setPassthroughEnabled(boolean enabled) {
        nativeSetPassthroughEnabled(mNativeRef, enabled);
    }

    /**
     * Style the XR_FB_passthrough layer. opacity is the texture opacity factor
     * [0,1]; edge[RGBA] is the edge-highlight colour (alpha 0 disables the edge).
     */
    public void setPassthroughStyle(float opacity, float edgeR, float edgeG,
                                    float edgeB, float edgeA) {
        nativeSetPassthroughStyle(mNativeRef, opacity, edgeR, edgeG, edgeB, edgeA);
    }

    /** Enable or disable XR_EXT_hand_tracking gesture processing (Quest only). */
    public void setHandTrackingEnabled(boolean enabled) {
        nativeSetHandTrackingEnabled(mNativeRef, enabled);
    }

    /**
     * Select the vertical tracking origin: false = eye-level (LOCAL, default),
     * true = floor-level (LOCAL_FLOOR or a STAGE-derived offset). No-op on
     * non-OpenXR backends.
     */
    public void setTrackingOrigin(boolean floor) {
        nativeSetTrackingOrigin(mNativeRef, floor);
    }

    /**
     * Apply a fixed-foveation level (0=OFF,1=LOW,2=MEDIUM,3=HIGH). No-op on
     * runtimes without XR_FB_foveation. (expo-pico fork.)
     */
    public void setFoveationLevel(int level, boolean dynamic) {
        nativeSetFoveationLevel(mNativeRef, level, dynamic);
    }

    /**
     * Negotiated OpenXR runtime facts as a String[10], or null if no immersive
     * instance exists. Read reflectively by expo-pico-core. (expo-pico fork.)
     */
    public String[] getRuntimeInfo() {
        return nativeGetRuntimeInfo(mNativeRef);
    }

    /* ----------     Common lifecycle methods    ---------- */

    public void destroy() {
        final long pending;
        synchronized (mCapturePoseLock) {
            mCapturePoseDestroyed = true;
            pending = mCapturePosePending;
        }
        if (pending != 0) mCapturePoseHandler.post(() ->
            finishCaptureHeadPose(pending, 11, 0, 0, 0, 0, null, false));
        if (mNativeFrameListener != null) {
            mNativeFrameListener.destroy();
        }
        mNativeFrameListener = null;

        synchronized (mCapturePoseLock) {
            if (mNativeRef != 0) nativeDestroyRenderer(mNativeRef);
            mNativeRef = 0;
        }
    }

    public void initializeGL(boolean framebufferSRGB) {
        nativeInitializeGL(mNativeRef, framebufferSRGB, ViroView.NATIVE_TESTING_MODE);
    }

    public void onStart() {
        if (mNativeRef != 0) {
            nativeOnStart(mNativeRef);
        }
    }

    public void onPause() {
        if (mNativeRef != 0) {
            nativeOnPause(mNativeRef);
        }
    }
    public void onResume() {
        if (mNativeRef != 0) {
            nativeOnResume(mNativeRef);
        }
    }

    public void onStop() {
        if (mNativeRef != 0) {
            nativeOnStop(mNativeRef);
        }
    }

    public void performHitTestWithPoint(int x, int y, boolean useBoundsOnly, HitTestListener callback) {
        nativePerformHitTestWithPoint(mNativeRef, x, y, useBoundsOnly, callback);
    }

    public void performARHitTestWithRay(float[] origin, float[] ray, boolean useBoundsOnly, HitTestListener callback) {
        nativePerformHitTestWithRay(mNativeRef, origin, ray, useBoundsOnly, callback);
    }

    public void onSurfaceCreated(Surface surface) { nativeOnSurfaceCreated(surface, mNativeRef); }
    public void onSurfaceChanged(Surface surface, int width, int height) { nativeOnSurfaceChanged(surface, width, height, mNativeRef); }

    /* ----------     Common other methods   ---------- */

    public void onKeyEvent(int keyCode, int action) {
        nativeOnKeyEvent(mNativeRef, keyCode, action);
    }
    public void onTouchEvent(int onTouchAction, float touchPosX, float touchPosY) {
        nativeOnTouchEvent(mNativeRef, onTouchAction, touchPosX, touchPosY);
    }

    public void onPinchEvent(int pinchState, float scaleFactor, float viewportX, float viewportY) {
        nativeOnPinchEvent(mNativeRef, pinchState, scaleFactor, viewportX, viewportY);
    }

    public void onRotateEvent(int rotateState, float rotateRadians, float viewportX, float viewportY) {
        nativeOnRotateEvent(mNativeRef, rotateState, rotateRadians, viewportX, viewportY);
    }

    public void setSceneController(long nativeSceneControllerRef) {
        nativeSetSceneController(mNativeRef, nativeSceneControllerRef);
    }
    public void setSceneController(long nativeSceneControllerRef, float duration) {
        nativeSetSceneControllerWithAnimation(mNativeRef, nativeSceneControllerRef, duration);
    };
    public void setPointOfView(Node node) {
        if (node != null) {
            nativeSetPointOfView(mNativeRef, node.mNativeRef);
        }
        else {
            nativeSetPointOfView(mNativeRef, 0);
        }
    }

    public Vector projectPoint(float x, float y, float z) {
        float[] pt = nativeProjectPoint(mNativeRef, x, y, z);
        if (pt != null) {
            return new Vector(pt);
        }
        else {
            return null;
        }
    }

    public Vector unprojectPoint(float x, float y, float z) {
        float[] pt = nativeUnprojectPoint(mNativeRef, x, y, z);
        if (pt != null) {
            return new Vector(pt);
        }
        else {
            return null;
        }
    }

    public void setClearColor(int color) {
        nativeSetClearColor(mNativeRef, color);
    }
    public String getHeadset() {
        return nativeGetHeadset(mNativeRef);
    }
    public String getController() {
        return nativeGetController(mNativeRef);
    }
    public void setDebugHUDEnabled(boolean enabled) { nativeSetDebugHUDEnabled(mNativeRef, enabled); }

    public void addFrameListener(NativeFrameListener frameListener) {
        nativeAddFrameListener(mNativeRef, frameListener.mNativeRef);
    }

    public void removeFrameListener(NativeFrameListener frameListener) {
        nativeRemoveFrameListener(mNativeRef, frameListener.mNativeRef);
    }

    public void setFrameListener(FrameListener listener) {
        if (mNativeFrameListener == null) {
            mNativeFrameListener = new NativeFrameListenerImpl(this);
        }

        if (listener == null) {
            if (mFrameListener != null) {
                removeFrameListener(mNativeFrameListener);
            }
        } else {
            if (mFrameListener == null) {
                addFrameListener(mNativeFrameListener);
            }
        }
        mFrameListener = listener;
    }

    public Vector getLastCameraPositionRealtime() {
        return new Vector(nativeGetCameraPositionRealtime(mNativeRef));
    }

    public Vector getLastCameraRotationRealtime() {
        return new Vector(nativeGetCameraRotationRealtime(mNativeRef));
    }

    public Vector getLastCameraForwardRealtime() {
        return new Vector(nativeGetCameraForwardRealtime(mNativeRef));
    }

    public float getFieldOfView() { return nativeGetFieldOfView(mNativeRef); }

    public void setCameraListener(CameraListener listener) {
        nativeSetCameraListener(mNativeRef, listener != null);
        mCameraListener = listener;
    }

    public void onCameraTransformationUpdate(float[] pos, float[] rotEuler, float[] forward) {
        if (mCameraListener == null) {
            return;
        }
        Vector vPos = new Vector(pos);
        Vector vRotEuler = new Vector(rotEuler);
        Vector vForward = new Vector(forward);
        mCameraListener.onTransformUpdate(vPos, vRotEuler, vForward);
    }

    public void onFrameDidRender() {
        if (mFrameListener != null) {
            mFrameListener.onDrawFrame();
        }
    }

    void setShadowsEnabled(boolean enabled) { nativeSetShadowsEnabled(mNativeRef, enabled); }
    void setHDREnabled(boolean enabled) { nativeSetHDREnabled(mNativeRef, enabled); }
    void setPBREnabled(boolean enabled) { nativeSetPBREnabled(mNativeRef, enabled); }
    void setBloomEnabled(boolean enabled) { nativeSetBloomEnabled(mNativeRef, enabled); }

    /* ----------     Native methods    ---------- */

    private native long nativeCreateRendererGVR(ClassLoader appClassLoader, Context context,
                                                AssetManager assets, PlatformUtil platformUtil, long nativeGvrContext,
                                                boolean enableShadows, boolean enableHDR, boolean enablePBR, boolean enableBloom);
    private native long nativeCreateRendererOVR(ClassLoader appClassLoader, Context context,
                                                ViroViewOVR view, Activity activity, AssetManager assets, PlatformUtil platformUtil,
                                                boolean enableShadows, boolean enableHDR, boolean enablePBR, boolean enableBloom);
    private native long nativeCreateRendererOpenXR(ClassLoader appClassLoader, Context context,
                                                   ViroViewOpenXR view, Activity activity, AssetManager assets, PlatformUtil platformUtil,
                                                   boolean enableShadows, boolean enableHDR, boolean enablePBR, boolean enableBloom);
    private native long nativeCreateRendererSceneView(ClassLoader appClassLoader, Context context,
                                                      ViroViewScene view, AssetManager assets, PlatformUtil platformUtil,
                                                      boolean enableShadows, boolean enableHDR, boolean enablePBR, boolean enableBloom);
    private native void nativeDestroyRenderer(long nativeRenderer);
    private native long nativeCreateFrameListener(long nativeRenderer);
    private native void nativeDestroyFrameListener(long frameListenerRef);
    private native void nativeInitializeGL(long nativeRenderer, boolean sRGBFramebuffer, boolean testingMode);
    private native void nativeSetVRModeEnabled(long nativeRenderer, boolean enabled);
    private native long nativeDrawFrame(long nativeRenderer);
    private native void nativeOnStart(long nativeRenderer);
    private native void nativeOnKeyEvent(long nativeRenderer, int keyCode, int action);
    private native void nativeOnTouchEvent(long nativeRenderer, int onTouchAction, float touchPosX, float touchPosY);
    private native void nativeOnPinchEvent(long nativeRenderer, int pinchState, float scaleFactor, float viewportX, float viewportY);
    private native void nativeOnRotateEvent(long nativeRenderer, int rotateState, float rotateRadians, float viewportX, float viewportY);
    private native void nativeOnPause(long nativeRenderer);
    private native void nativeOnResume(long nativeRenderer);
    private native void nativeOnStop(long nativeRenderer);
    private native void nativeOnSurfaceCreated(Surface surface, long nativeRenderer);
    private native void nativeOnSurfaceChanged(Surface surface, int width, int height, long nativeRenderer);
    private native void nativeOnSurfaceDestroyed(long nativeRenderer);
    private native void nativeSetSceneController(long nativeRenderer, long nativeScene);
    private native void nativeSetSceneControllerWithAnimation(long nativeRenderer, long nativeScene, float duration);
    private native void nativeSetPointOfView(long nativeRenderer, long nodeRef);
    private native String nativeGetHeadset(long nativeRenderer);
    private native String nativeGetController(long nativeRenderer);
    private native void nativeSetDebugHUDEnabled(long nativeRenderer, boolean enabled);
    private native void nativeRecenterTracking(long nativeRenderer);
    private native void nativeRequestCaptureHeadPose(long nativeRenderer, long requestId,
                                                     long timestampNanos, int clockDomain, long expectedSpaceGeneration);
    private native long nativeCapturePoseGeneration(long nativeRenderer);
    private native void nativeSetPassthroughEnabled(long nativeRenderer, boolean enabled);
    private native void nativeSetPassthroughStyle(long nativeRenderer, float opacity,
                                                  float edgeR, float edgeG, float edgeB, float edgeA);
    private native void nativeSetHandTrackingEnabled(long nativeRenderer, boolean enabled);
    private native void nativeSetTrackingOrigin(long nativeRenderer, boolean floor);
    private native void nativeSetFoveationLevel(long nativeRenderer, int level, boolean dynamic);
    private native String[] nativeGetRuntimeInfo(long nativeRenderer);
    private native void nativeSetClearColor(long sceneRef, int color);
    private native void nativeSetShadowsEnabled(long nativeRef, boolean enabled);
    private native void nativeSetHDREnabled(long nativeRef, boolean enabled);
    private native void nativeSetPBREnabled(long nativeRef, boolean enabled);
    private native void nativeSetBloomEnabled(long nativeRef, boolean enabled);
    private native void nativeAddFrameListener(long nativeRenderer, long frameListener);
    private native void nativeRemoveFrameListener(long nativeRenderer, long frameListener);
    private native float[] nativeGetCameraPositionRealtime(long nativeRenderer);
    private native float[] nativeGetCameraRotationRealtime(long nativeRenderer);
    private native float[] nativeGetCameraForwardRealtime(long nativeRenderer);
    private native void nativeSetCameraListener(long nativeRenderer, boolean enabled);
    private native float[] nativeProjectPoint(long nativeRenderer, float x, float y, float z);
    private native float[] nativeUnprojectPoint(long nativeRenderer, float x, float y, float z);
    private native float nativeGetFieldOfView(long nativeRef);
    private native void nativePerformHitTestWithPoint(long nativeRef, int x, int y, boolean boundsOnly, HitTestListener callback);
    private native void nativePerformHitTestWithRay(long nativeRef, float origin[], float ray[], boolean boundsOnly, HitTestListener callback);
    private native long nativeGetChoreographerRef(long nativeRenderer);
    private native void nativeReleaseChoreographerRef(long choreographerRef);

    /**
     * Return a persistent native handle to this renderer's VROChoreographer.
     * Caller MUST call {@link #releaseChoreographerRef(long)} when done.
     * Used by extension passes (e.g. @viro-external/splat-pass) to insert a
     * custom VRORenderPass via VROChoreographer::setCustomPostOpaquePass.
     */
    public long getChoreographerRef() {
        if (mNativeRef == 0) return 0;
        return nativeGetChoreographerRef(mNativeRef);
    }

    public void releaseChoreographerRef(long choreographerRef) {
        if (choreographerRef != 0) {
            nativeReleaseChoreographerRef(choreographerRef);
        }
    }

}
