//
//  VROOpenXRRivePanelFallback.h
//  ViroRenderer
//
//  Scene-mesh fallback texture for a Rive compositor panel that lost its layer
//  (budget eviction or XR_ERROR_LAYER_LIMIT_EXCEEDED). It is an ordinary
//  VROTexture a host puts on a quad material with VROBlendMode::PremultiplyAlpha,
//  in place of the evicted layer.
//
//  Same pixels as the layer: each update copies the newest frame of the same
//  Rive ring with the same panel copy program and the same coverage function
//  the finish pass uses (VROPanelShaderSources.h), into a mipmapped
//  GL_TEXTURE_2D. With sRGB storage (linear rendering) the copy writes linear
//  premultiplied values and the hardware encodes them, exactly like an sRGB
//  swapchain; with RGBA8 storage it writes the encoded premultiplied bytes.
//  Mips are built from premultiplied values.
//
//  Threads: create, update and destroy GL objects on the XR render thread
//  with the XR context current (deletion goes through the driver's deletion
//  queue, so the destructor may run anywhere).
//

#pragma once

#include "VROPanelCoverage.h"
#include "VRORivePanel.h"
#include "VROTexture.h"

#include <GLES3/gl3.h>

#include <memory>

class VRODriverOpenGL;

class VROOpenXRRivePanelFallback : public VROTexture {
public:
    VROOpenXRRivePanelFallback(std::shared_ptr<VRORivePanel> panel, int widthPx, int heightPx,
                               VROPanelCornerRadii radiiPx);
    ~VROOpenXRRivePanelFallback();

    // XR render thread. Allocates the texture (sRGB storage when the driver
    // renders in linear space), the copy program and the framebuffer.
    bool init(std::shared_ptr<VRODriverOpenGL> driver);

    // XR render thread. Copies the newest published Rive frame, if any, and
    // rebuilds the mips. False when nothing has been published or init failed.
    bool update();

private:
    std::shared_ptr<VRORivePanel> _panel;
    int _widthPx;
    int _heightPx;
    VROPanelCornerRadii _radii;
    std::weak_ptr<VRODriverOpenGL> _driver;
    bool _ready = false;
    bool _hasSrgbWriteControl = false;
    GLuint _texture = 0;
    GLuint _framebuffer = 0;
    GLuint _program = 0;
    GLuint _vertexArray = 0;
    GLuint _vertexBuffer = 0;
    GLint _sizeUniform = -1;
    GLint _radiusUniform = -1;
};
