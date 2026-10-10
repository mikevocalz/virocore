// Independent native OpenXR panel swapchains. Render-thread-owned resource.
// See docs/OPENXR_PANEL_COMPOSITOR.md for producer and lifecycle rules.
#pragma once

#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif
#ifndef XR_USE_PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID
#endif
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class VROOpenXRPanelShape { Quad, Cylinder };

struct VROOpenXRPanelDesc {
    std::string id;
    VROOpenXRPanelShape shape = VROOpenXRPanelShape::Quad;
    XrPosef pose = { {0, 0, 0, 1}, {0, 0, -2} };
    float widthMeters = 1.6f;
    float heightMeters = 0.9f;
    // Required for cylinder: > 0 and arc width / radius < 2pi.
    float radiusMeters = 0.0f;
    uint32_t widthPixels = 1024;
    uint32_t heightPixels = 576;
    bool visible = true;
};

/**
 * Called for every acquired panel swapchain image. The producer draws actual
 * UI content into the supplied OpenGL ES 2D texture on the OpenXR render
 * thread and returns true only when rendering/submission succeeded.
 * No blank placeholder or stale panel is submitted after an unsuccessful draw.
 */
using VROOpenXRPanelProducer = std::function<bool(GLuint, uint32_t, uint32_t)>;

class VROOpenXRPanelCompositor final {
public:
    VROOpenXRPanelCompositor(XrSession session, int64_t rgbaFormat,
                             uint32_t maxLayers, bool cylinderEnabled);
    ~VROOpenXRPanelCompositor();

    VROOpenXRPanelCompositor(const VROOpenXRPanelCompositor &) = delete;
    VROOpenXRPanelCompositor &operator=(const VROOpenXRPanelCompositor &) = delete;

    // All methods must run on the XR render thread, except construction and
    // destruction at session startup/teardown after the render loop is stopped.
    bool addPanel(const VROOpenXRPanelDesc &desc, VROOpenXRPanelProducer producer);
    bool updatePanelPose(const std::string &id, const XrPosef &pose);
    bool setPanelVisible(const std::string &id, bool visible);
    bool removePanel(const std::string &id);
    void clear();

    // After projection/passthrough setup, before xrEndFrame. Produces layer
    // pointers owned by this object, valid until the next mutation.
    void appendLayers(XrSpace appSpace, uint32_t remainingLayerSlots,
                      std::vector<const XrCompositionLayerBaseHeader *> &layers);
    bool supportsCylinder() const { return _cylinderEnabled; }
    size_t count() const { return _panels.size(); }

private:
    struct Panel {
        VROOpenXRPanelDesc desc;
        VROOpenXRPanelProducer producer;
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageOpenGLESKHR> images;
        XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
        XrCompositionLayerCylinderKHR cylinder = { XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR };
    };

    XrSession _session;
    int64_t _rgbaFormat;
    uint32_t _maxLayers;
    bool _cylinderEnabled;
    std::vector<Panel> _panels;

    Panel *find(const std::string &id);
    bool createSwapchain(Panel &panel);
    void destroySwapchain(Panel &panel);
};
