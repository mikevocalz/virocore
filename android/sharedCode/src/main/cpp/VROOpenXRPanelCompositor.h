// Independent native OpenXR panel swapchains. Render-thread-owned resource.
// See docs/OPENXR_PANEL_COMPOSITOR.md for producer and lifecycle rules.
#pragma once

#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif
#ifndef XR_USE_PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID
#endif
#include <jni.h>  // OpenXR Android platform declarations use jobject
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class VROOpenXRPanelShape { Quad, Cylinder };

// Rejection is observable by the JNI/Expo host; do not collapse failures to false.
enum class VROOpenXRPanelError {
    None,
    SessionUnavailable,
    EmptyId,
    MissingProducer,
    DuplicateId,
    LayerBudgetExceeded,
    InvalidPose,
    InvalidDimensions,
    InvalidPixelSize,
    CylinderUnavailable,
    InvalidCylinderRadius,
    InvalidCylinderAngle,
    SwapchainFailed,
};
const char *VROOpenXRPanelErrorMessage(VROOpenXRPanelError error);

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
 * Draws changed UI content into an acquired GLES swapchain image on the XR
 * renderer thread. Returns true only when the texture contains a complete
 * frame. The producer MUST write premultiplied alpha: composition sets
 * BLEND_TEXTURE_SOURCE_ALPHA_BIT, not UNPREMULTIPLIED_ALPHA_BIT.
 *
 * On a failed draw the compositor retains the acquired image for a retry,
 * without releasing it or replacing the last successfully submitted frame.
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
    VROOpenXRPanelError addPanel(const VROOpenXRPanelDesc &desc,
                                  VROOpenXRPanelProducer producer);
    // Call when the producer has new pixels; pose changes need no redraw.
    bool invalidatePanelContent(const std::string &id);
    bool updatePanelPose(const std::string &id, const XrPosef &pose);
    bool setPanelVisible(const std::string &id, bool visible);
    bool removePanel(const std::string &id);
    void clear();

    // After projection/passthrough setup, before xrEndFrame. Produces layer
    // pointers owned by this object, valid until the next mutation.
    void appendLayers(XrSpace appSpace, uint32_t remainingLayerSlots,
                      XrDuration predictedDisplayPeriod,
                      std::vector<const XrCompositionLayerBaseHeader *> &layers);
    bool supportsCylinder() const { return _cylinderEnabled; }
    size_t count() const { return _panels.size(); }

private:
    struct Panel {
        VROOpenXRPanelDesc desc;
        VROOpenXRPanelProducer producer;
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageOpenGLESKHR> images;
        bool dirty = true;
        bool acquired = false;
        bool waited = false;
        bool readyToRelease = false;
        bool hasReleasedImage = false;
        uint32_t acquiredIndex = 0;
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
