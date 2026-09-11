// VRODriverOpenGLAndroidOpenXR.h
// ViroRenderer
//
// Android OpenGL driver for Meta Quest / OpenXR. Identical structure to
// VRODriverOpenGLAndroidOVR but returns a VRODisplayOpenGLOpenXR display
// and reports Linear color rendering (Quest uses sRGB swapchain images).
//
// EGL context ownership: the context is created in VROSceneRendererOpenXR
// before xrCreateSession and must remain current on the render thread.
// This driver does NOT create the EGL context; it only manages the Viro
// abstractions that sit above it.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VRODRIVEROPENGLANDROIDOPENXR_H
#define ANDROID_VRODRIVEROPENGLANDROIDOPENXR_H

#include "VRODriverOpenGLAndroid.h"
#include "VRODisplayOpenGLOpenXR.h"

class VRODriverOpenGLAndroidOpenXR : public VRODriverOpenGLAndroid {
public:

    VRODriverOpenGLAndroidOpenXR(std::shared_ptr<gvr::AudioApi> gvrAudio)
        : VRODriverOpenGLAndroid(gvrAudio) {}

    virtual ~VRODriverOpenGLAndroidOpenXR() {}

    /*
     * The renderer picks the swapchain format at session start and sets the
     * matching colour mode here. An sRGB swapchain (GL_SRGB8_ALPHA8) gamma-
     * encodes on write, so Viro renders Linear; a plain GL_RGBA8 swapchain does
     * not, so Viro must gamma-encode itself (NonLinear) or the image is dark.
     * Defaults to Linear because every conformant runtime enumerates sRGB;
     * NonLinear is the fallback path some PICO firmware forces.
     */
    void setColorRenderingMode(VROColorRenderingMode mode) { _colorMode = mode; }
    VROColorRenderingMode getColorRenderingMode() override {
        return _colorMode;
    }

    /*
     * Returns the VRODisplayOpenGLOpenXR singleton. The renderer calls
     * display->setSwapchainImage() each frame before binding.
     */
    std::shared_ptr<VRORenderTarget> getDisplay() override {
        if (!_display) {
            std::shared_ptr<VRODriverOpenGL> driver = shared_from_this();
            _display = std::make_shared<VRODisplayOpenGLOpenXR>(driver);
        }
        return _display;
    }

    /*
     * Convenience cast used by the renderer to call setSwapchainImage().
     */
    std::shared_ptr<VRODisplayOpenGLOpenXR> getOpenXRDisplay() {
        return std::dynamic_pointer_cast<VRODisplayOpenGLOpenXR>(getDisplay());
    }

private:
    std::shared_ptr<VRORenderTarget> _display;
    VROColorRenderingMode _colorMode = VROColorRenderingMode::Linear;
};

#endif  // ANDROID_VRODRIVEROPENGLANDROIDOPENXR_H
