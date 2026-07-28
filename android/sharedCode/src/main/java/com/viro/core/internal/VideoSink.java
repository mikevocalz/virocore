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
import android.util.Log;
import android.view.Surface;

import com.viro.core.FrameListener;

public class VideoSink implements SurfaceTexture.OnFrameAvailableListener, FrameListener {

    private static final String TAG = "VideoSink";

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

    private int mFramesAvailable;
    private int mFramesConsumed;

    /**
     * Create a new VideoSink wrapping the given native OpenGL
     * texture ID.
     *
     * @param textureId The native texture ID.
     */
    public VideoSink(int textureId) {
        mSurfaceTexture = new SurfaceTexture(textureId);
        mSurfaceTexture.setOnFrameAvailableListener(this);
        mSurface = new Surface(mSurfaceTexture);

        synchronized (this) {
            mSurfacedUpdated = false;
        }
    }

    public VideoSink(int textureId, int width, int height) {
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
                mFramesConsumed += 1;
                if (mFramesConsumed <= 3 || mFramesConsumed % 60 == 0) {
                    Log.i(TAG, "updateTexImage consumed frame " + mFramesConsumed);
                }
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
        mFramesAvailable += 1;
        if (mFramesAvailable <= 3 || mFramesAvailable % 60 == 0) {
            Log.i(TAG, "onFrameAvailable frame " + mFramesAvailable);
        }
    }

    public Surface getSurface() {
        return mSurface;
    }

    public void releaseSurface() {
        mSurface.release();
    }
}
