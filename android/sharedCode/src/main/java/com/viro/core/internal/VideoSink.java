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

package com.viro.core.internal;

import android.graphics.SurfaceTexture;
import android.view.Surface;

import com.viro.core.FrameListener;

public class VideoSink implements SurfaceTexture.OnFrameAvailableListener, FrameListener {

    /**
     * The underlying Android Graphics surface.
     */
    private Surface mSurface;

    /**
     * The underlying GL surface texture.
     */
    private SurfaceTexture mSurfaceTexture;

    /**
     * True if the surface's data has been updated.
     */
    private boolean mSurfacedUpdated;

    /**
     * The native OpenGL texture (GL_TEXTURE_EXTERNAL_OES) this sink feeds.
     */
    private final int mTextureId;

    /**
     * Create a new VideoSink wrapping the given native OpenGL
     * texture ID.
     *
     * @param textureId The native texture ID.
     */
    public VideoSink(int textureId) {
        mTextureId = textureId;
        mSurfaceTexture = new SurfaceTexture(textureId);
        mSurfaceTexture.setOnFrameAvailableListener(this);
        mSurface = new Surface(mSurfaceTexture);

        synchronized (this) {
            mSurfacedUpdated = false;
        }
    }

    public VideoSink(int textureId, int width, int height) {
        mTextureId = textureId;
        mSurfaceTexture = new SurfaceTexture(textureId);
        mSurfaceTexture.setOnFrameAvailableListener(this);
        mSurfaceTexture.setDefaultBufferSize(width, height);
        mSurface = new Surface(mSurfaceTexture);

        synchronized (this) {
            mSurfacedUpdated = false;
        }
    }

    @Override
    public void onDrawFrame() {
        synchronized (this) {
            if (mSurfacedUpdated) {
                mSurfaceTexture.updateTexImage();
                mSurfacedUpdated = false;
                // Rendering thread, GL context current: let the native texture
                // that samples this frame rebuild its mipmapped copy.
                nativeOnFrameLatched(mTextureId);
            }
        }
    }

    @Override
    synchronized public void onFrameAvailable(SurfaceTexture surface) {
        /*
         * SurfaceTexture calls here when it has new
         * data available. Call may come in from any thread,
         * so synchronized. No OpenGL calls can be done here.
         */
        mSurfacedUpdated = true;
    }

    public Surface getSurface() {
        return mSurface;
    }

    public void releaseSurface() {
        mSurface.release();
    }

    /*
     Implemented in VROAndroidViewTexture.cpp. A no-op for textures that keep
     no mipmapped copy (for example the video textures behind AVPlayer).
     */
    private static native void nativeOnFrameLatched(int textureId);
}
