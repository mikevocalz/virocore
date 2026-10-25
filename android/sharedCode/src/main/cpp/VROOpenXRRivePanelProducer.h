//
//  VROOpenXRRivePanelProducer.h
//  ViroRenderer
//
//  The first real VROOpenXRPanelProducer (viro-external ADR 0001 route B2): a
//  finish pass that copies the newest frame of a VRORivePanel texture ring
//  into an acquired OpenXR swapchain image.
//
//  Per call, on the XR render thread with the XR context current:
//    1. VRORivePanel::beginFrameRead(): newest ring frame, and glWaitSync on
//       the fence Rive inserted after it (GPU-side wait, no CPU block).
//    2. One full-target triangle with the panel copy program
//       (VROPanelShaderSources.h): sample the ring texture (premultiplied,
//       sRGB-encoded), and for a GL_SRGB8_ALPHA8 swapchain unpremultiply,
//       decode to linear and premultiply again; then multiply all four
//       channels by the shared rounded-rect coverage (VROPanelCoverage.h).
//       A GL_RGBA8 swapchain gets the premultiplied encoded bytes, matching
//       how the eye buffers are written in that mode.
//    3. VRORivePanel::endFrameRead(): fence after the draw so Rive does not
//       reuse the texture before the copy has read it.
//  Returns false, keeping the image acquired for a retry (compositor rule),
//  until Rive has published a first frame or when the framebuffer is
//  incomplete.
//
//  GL state: the pass runs on Viro's context between Viro's frames. It saves
//  and restores exactly the state it changes with VROPanelGLStateGuard, so
//  VRODriverOpenGL's cached state stays true and invalidateGLStateCache() is
//  not called. It owns one framebuffer, one program, one vertex array and one
//  vertex buffer, created on the XR context in create() and deleted in the
//  destructor when that context is current (at session teardown it is not,
//  and they go with the context).
//
//  Teardown order (docs/OPENXR_PANEL_COMPOSITOR.md): the compositor destroys
//  the panel swapchain after glFinish, then drops the producer callback; the
//  producer's destructor deletes its GL objects and then releases the Rive
//  panel, whose worker deletes the ring and its shared context last.
//

#pragma once

#include "VROOpenXRPanelCompositor.h"
#include "VROPanelCoverage.h"
#include "VRORivePanel.h"

#include <GLES3/gl3.h>

#include <memory>
#include <string>

class VROOpenXRRivePanelProducer {
public:
    // XR render thread, XR context current. swapchainFormat is the panel
    // swapchain's GL format (VROSceneRendererOpenXR's _panelSwapchainFormat).
    // radiiPx are the per-axis corner radii in swapchain pixels.
    static std::shared_ptr<VROOpenXRRivePanelProducer> create(std::shared_ptr<VRORivePanel> panel,
                                                              int64_t swapchainFormat,
                                                              VROPanelCornerRadii radiiPx,
                                                              std::string *error);
    ~VROOpenXRRivePanelProducer();
    VROOpenXRRivePanelProducer(const VROOpenXRRivePanelProducer &) = delete;
    VROOpenXRRivePanelProducer &operator=(const VROOpenXRRivePanelProducer &) = delete;

    // The finish pass; signature of VROOpenXRPanelProducer.
    bool draw(GLuint image, uint32_t widthPx, uint32_t heightPx);

    // Wraps draw() in a VROOpenXRPanelProducer that keeps the producer alive
    // until the compositor drops the callback.
    static VROOpenXRPanelProducer bind(const std::shared_ptr<VROOpenXRRivePanelProducer> &producer);

    const std::shared_ptr<VRORivePanel> &panel() const { return _panel; }
    bool writesLinear() const { return _linearOutput; }

private:
    VROOpenXRRivePanelProducer() = default;
    bool createGLResources(std::string *error);

    std::shared_ptr<VRORivePanel> _panel;
    VROPanelCornerRadii _radii;
    bool _linearOutput = false;
    bool _hasSrgbWriteControl = false;
    EGLContext _context = EGL_NO_CONTEXT;
    GLuint _framebuffer = 0;
    GLuint _program = 0;
    GLuint _vertexArray = 0;
    GLuint _vertexBuffer = 0;
    GLint _sizeUniform = -1;
    GLint _radiusUniform = -1;
};
