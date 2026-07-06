//
//  Copyright (c) 2026-present, ViroMedia fork (mikevocalz/virocore).
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

package com.viro.core;

import android.view.Surface;

/**
 * ExternalSurfaceTexture is a {@link Texture} backed by an Android
 * {@link Surface} that an <i>external producer</i> (WebGPU, Skia, MediaCodec,
 * a Canvas renderer — anything that can draw into a Surface) renders into.
 * <p>
 * Under the hood this reuses the exact machinery of
 * {@link AndroidViewTexture} / VideoTexture: on the rendering thread the
 * renderer generates a <tt>GL_TEXTURE_EXTERNAL_OES</tt> texture, wraps it in a
 * {@code SurfaceTexture}-backed VideoSink (registered as a per-frame
 * {@link FrameListener} that calls {@code updateTexImage()} each rendered
 * frame), and hands the producer-facing {@link Surface} back through
 * {@link #setVideoSink(Surface)}.
 * <p>
 * Unlike {@link AndroidViewTexture} this class does NOT attach any Android
 * View machinery — the Surface is exposed raw so the caller can hand it to a
 * GPU producer. Because the renderer initializes asynchronously on its own
 * thread, the Surface is delivered via {@link SurfaceListener} rather than
 * being available at construction time.
 * <p>
 * The texture can be applied to any {@link Material} via
 * {@link Material#setDiffuseTexture(Texture)}; the engine rewrites the diffuse
 * sampler to <tt>samplerExternalOES</tt> automatically for EGL-image textures.
 */
public class ExternalSurfaceTexture extends Texture {

    /**
     * Invoked on the rendering thread once the producer-facing {@link Surface}
     * exists. Implementations must not perform GL work.
     */
    public interface SurfaceListener {
        void onSurfaceReady(Surface surface);
    }

    private volatile Surface mSurface;
    private volatile SurfaceListener mListener;

    /**
     * Construct a new ExternalSurfaceTexture sized to the producer's pixel
     * resolution.
     *
     * @param viroContext The {@link ViroContext}, obtained from an active
     *                    {@link ViroView#getViroContext()}.
     * @param pxWidth     The width of the texture in pixels.
     * @param pxHeight    The height of the texture in pixels.
     * @param listener    Invoked (on the rendering thread) when the Surface is
     *                    ready for the producer. May be null; poll
     *                    {@link #getSurface()} instead.
     */
    public ExternalSurfaceTexture(ViroContext viroContext, int pxWidth, int pxHeight,
                                  SurfaceListener listener) {
        mWidth = pxWidth;
        mHeight = pxHeight;
        mListener = listener;
        mNativeRef = nativeCreateExternalSurfaceTexture(viroContext.mNativeRef, pxWidth, pxHeight);
    }

    /**
     * The producer-facing {@link Surface}, or null while the renderer is still
     * initializing the underlying GL texture.
     */
    public Surface getSurface() {
        return mSurface;
    }

    /**
     * Release native resources. The producer must stop writing to the Surface
     * before this is called. Idempotent.
     */
    public void dispose() {
        mListener = null;
        mSurface = null;
        if (mNativeRef != 0) {
            nativeDeleteExternalSurfaceTexture(mNativeRef);
            mNativeRef = 0;
        }
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            dispose();
        } finally {
            super.finalize();
        }
    }

    // Called by the renderer (JNI, rendering thread) once the VideoSink's
    // Surface exists. Signature must remain exactly
    // `setVideoSink(Landroid/view/Surface;)V` — resolved reflectively from
    // VROAndroidViewTexture::init().
    void setVideoSink(Surface surface) {
        mSurface = surface;
        SurfaceListener listener = mListener;
        if (listener != null) {
            listener.onSurfaceReady(surface);
        }
    }

    /*
     Native functions implemented in ExternalSurfaceTexture_JNI.cpp.
     */
    private native long nativeCreateExternalSurfaceTexture(long renderContextRef, int width, int height);
    private native void nativeDeleteExternalSurfaceTexture(long nativeRef);
}
