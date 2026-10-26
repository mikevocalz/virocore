// Independent native OpenXR panel swapchains. Render-thread-owned resource.
// See docs/OPENXR_PANEL_COMPOSITOR.md for producer and lifecycle rules.
#pragma once

#include "VROOpenXRPanelImageState.h"

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
    // No producer could be built for the requested content (for a Rive panel:
    // rive-runtime not in this ABI, the shared EGL context failed, or the
    // finish pass did not compile).
    ProducerUnavailable,
};
const char *VROOpenXRPanelErrorMessage(VROOpenXRPanelError error);

struct VROOpenXRPanelDesc {
    std::string id;
    VROOpenXRPanelShape shape = VROOpenXRPanelShape::Quad;
    // Quad: centre of the panel. Cylinder: centre of the circle the arc lies
    // on (OpenXR XrCompositionLayerCylinderKHR), so the visible surface sits
    // radiusMeters in front of it along -Z.
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

// Where a visible panel's pixels were shown on the last frame that composed
// layers. Pending until the panel first gets a slot or is first refused one.
enum class VROOpenXRPanelPresentation { Pending, Compositor, Evicted };

enum class VROOpenXRPanelEvictionReason {
    None,
    // No free layer slot this frame: the runtime's maxLayerCount, minus
    // projection and passthrough and the panels ahead in registration order.
    LayerBudget,
    // The runtime rejected a frame with XR_ERROR_LAYER_LIMIT_EXCEEDED and the
    // renderer lowered its layer cap below maxLayerCount.
    LayerLimitExceeded,
};
const char *VROOpenXRPanelPresentationName(VROOpenXRPanelPresentation presentation);
const char *VROOpenXRPanelEvictionReasonName(VROOpenXRPanelEvictionReason reason);

// XR render thread, from appendLayers, on every change of a visible panel's
// presentation. An Evicted panel is not on screen until the host shows its
// scene-mesh fallback; it returns to Compositor by itself when a slot frees.
// Runs between appendLayers and xrEndFrame, while the layer list points into
// this compositor: it must not add, remove or update panels. Post such work
// to the next frame (VROSceneRendererOpenXR forwards to its host that way).
using VROOpenXRPanelPresentationListener =
    std::function<void(const std::string &id, VROOpenXRPanelPresentation presentation,
                       VROOpenXRPanelEvictionReason reason)>;

class VROOpenXRPanelCompositor final {
public:
    // maxLayers is the panel budget, already excluding projection/passthrough.
    VROOpenXRPanelCompositor(XrSession session, int64_t rgbaFormat,
                             uint32_t maxLayers, bool cylinderEnabled);
    ~VROOpenXRPanelCompositor();

    VROOpenXRPanelCompositor(const VROOpenXRPanelCompositor &) = delete;
    VROOpenXRPanelCompositor &operator=(const VROOpenXRPanelCompositor &) = delete;

    // All methods must run on the XR render thread, except construction and
    // destruction at session startup/teardown after the render loop is stopped.
    VROOpenXRPanelError addPanel(const VROOpenXRPanelDesc &desc,
                                  VROOpenXRPanelProducer producer);
    // What addPanel(desc, producer) would return for a producer, short of
    // allocating the swapchain. Lets a caller refuse a bad desc before it
    // builds a costly producer.
    VROOpenXRPanelError validatePanel(const VROOpenXRPanelDesc &desc) const;
    // Call when the producer has new pixels; pose changes need no redraw.
    bool invalidatePanelContent(const std::string &id);
    bool updatePanelPose(const std::string &id, const XrPosef &pose);
    bool setPanelVisible(const std::string &id, bool visible);
    bool removePanel(const std::string &id);
    void clear();
    // Drain producer work while the owning GLES context is still current.
    // Required before releasing that context at render-thread shutdown.
    void finishPendingGpuWork();

    void setPresentationListener(VROOpenXRPanelPresentationListener listener);
    VROOpenXRPanelPresentation presentation(const std::string &id) const;

    // After projection/passthrough setup, before xrEndFrame. Produces layer
    // pointers owned by this object, valid until the next mutation. Visible
    // panels that get no slot are reported Evicted with shortReason; panels
    // earlier in registration order take slots first.
    void appendLayers(XrSpace appSpace, uint32_t remainingLayerSlots,
                      XrDuration predictedDisplayPeriod,
                      std::vector<const XrCompositionLayerBaseHeader *> &layers,
                      VROOpenXRPanelEvictionReason shortReason =
                          VROOpenXRPanelEvictionReason::LayerBudget);
    bool supportsCylinder() const { return _cylinderEnabled; }
    // The panel budget given at construction. A ceiling, not a promise that
    // this many panels composite every frame (see appendLayers).
    uint32_t maxPanels() const { return _maxLayers; }
    size_t count() const { return _panels.size(); }

private:
    struct Panel {
        VROOpenXRPanelDesc desc;
        VROOpenXRPanelProducer producer;
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageOpenGLESKHR> images;
        VROOpenXRPanelImageState imageState;
        XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
        XrCompositionLayerCylinderKHR cylinder = { XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR };
        VROOpenXRPanelPresentation presentation = VROOpenXRPanelPresentation::Pending;
    };

    XrSession _session;
    int64_t _rgbaFormat;
    uint32_t _maxLayers;
    bool _cylinderEnabled;
    bool _hasPendingGpuWork = false;
    std::vector<Panel> _panels;
    VROOpenXRPanelPresentationListener _presentationListener;

    Panel *find(const std::string &id);
    const Panel *find(const std::string &id) const;
    void present(Panel &panel, VROOpenXRPanelPresentation presentation,
                 VROOpenXRPanelEvictionReason reason);
    bool createSwapchain(Panel &panel);
    void destroySwapchain(Panel &panel);
};
