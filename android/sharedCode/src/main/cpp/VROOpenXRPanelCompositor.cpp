#include "VROOpenXRPanelCompositor.h"

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace {
constexpr float kFullCircle = 6.2831853071795864769f;

bool validPose(const XrPosef &pose) {
    const auto &p = pose.position;
    const auto &q = pose.orientation;
    const float len2 = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
        std::isfinite(q.w) && len2 > 0.99f && len2 < 1.01f;
}
}

const char *VROOpenXRPanelErrorMessage(VROOpenXRPanelError error) {
    switch (error) {
        case VROOpenXRPanelError::None: return "none";
        case VROOpenXRPanelError::SessionUnavailable: return "OpenXR session unavailable";
        case VROOpenXRPanelError::EmptyId: return "panel ID must not be empty";
        case VROOpenXRPanelError::MissingProducer: return "panel texture producer missing";
        case VROOpenXRPanelError::DuplicateId: return "panel ID already registered";
        case VROOpenXRPanelError::LayerBudgetExceeded: return "runtime panel layer budget exceeded";
        case VROOpenXRPanelError::InvalidPose: return "panel pose must be finite and orientation normalized";
        case VROOpenXRPanelError::InvalidDimensions: return "panel width and height must be positive finite metres";
        case VROOpenXRPanelError::InvalidPixelSize: return "panel pixel dimensions must be between 16 and 4096";
        case VROOpenXRPanelError::CylinderUnavailable: return "XR_KHR_composition_layer_cylinder unavailable";
        case VROOpenXRPanelError::InvalidCylinderRadius: return "cylinder radius must be finite and greater than zero";
        case VROOpenXRPanelError::InvalidCylinderAngle: return "cylinder arc angle must be smaller than 2pi";
        case VROOpenXRPanelError::SwapchainFailed: return "OpenXR panel swapchain allocation failed";
        case VROOpenXRPanelError::ProducerUnavailable: return "panel texture producer could not be created";
    }
    return "unknown OpenXR panel error";
}

const char *VROOpenXRPanelPresentationName(VROOpenXRPanelPresentation presentation) {
    switch (presentation) {
        case VROOpenXRPanelPresentation::Pending: return "pending";
        case VROOpenXRPanelPresentation::Compositor: return "compositor";
        case VROOpenXRPanelPresentation::Evicted: return "evicted";
    }
    return "unknown";
}

const char *VROOpenXRPanelEvictionReasonName(VROOpenXRPanelEvictionReason reason) {
    switch (reason) {
        case VROOpenXRPanelEvictionReason::None: return "none";
        case VROOpenXRPanelEvictionReason::LayerBudget: return "layer budget";
        case VROOpenXRPanelEvictionReason::LayerLimitExceeded: return "XR_ERROR_LAYER_LIMIT_EXCEEDED";
    }
    return "unknown";
}

VROOpenXRPanelCompositor::VROOpenXRPanelCompositor(
    XrSession session, int64_t rgbaFormat,
    uint32_t maxLayers, bool cylinderEnabled)
    : _session(session), _rgbaFormat(rgbaFormat),
      _maxLayers(maxLayers), _cylinderEnabled(cylinderEnabled) {}

VROOpenXRPanelCompositor::~VROOpenXRPanelCompositor() { clear(); }

VROOpenXRPanelCompositor::Panel *VROOpenXRPanelCompositor::find(const std::string &id) {
    auto it = std::find_if(_panels.begin(), _panels.end(),
                           [&id](const Panel &p) { return p.desc.id == id; });
    return it == _panels.end() ? nullptr : &*it;
}

const VROOpenXRPanelCompositor::Panel *VROOpenXRPanelCompositor::find(const std::string &id) const {
    auto it = std::find_if(_panels.begin(), _panels.end(),
                           [&id](const Panel &p) { return p.desc.id == id; });
    return it == _panels.end() ? nullptr : &*it;
}

void VROOpenXRPanelCompositor::setPresentationListener(VROOpenXRPanelPresentationListener listener) {
    _presentationListener = std::move(listener);
}

VROOpenXRPanelPresentation VROOpenXRPanelCompositor::presentation(const std::string &id) const {
    const Panel *panel = find(id);
    return panel ? panel->presentation : VROOpenXRPanelPresentation::Pending;
}

void VROOpenXRPanelCompositor::present(Panel &panel, VROOpenXRPanelPresentation presentation,
                                       VROOpenXRPanelEvictionReason reason) {
    if (panel.presentation == presentation) return;
    panel.presentation = presentation;
    if (_presentationListener) {
        // The listener must not mutate the compositor (see the header); the
        // id is copied anyway so a misbehaving one cannot read a moved panel.
        const std::string id = panel.desc.id;
        _presentationListener(id, presentation, reason);
    }
}

namespace {

// Panel swapchains alive across every compositor in the process. Logged on
// each create and destroy (never per frame) so a count that does not return
// to its baseline after a reload or background cycle shows a leak.
std::atomic<int> sLivePanelSwapchains{0};

} // namespace

bool VROOpenXRPanelCompositor::createSwapchain(Panel &panel) {
    XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                      XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = _rgbaFormat;
    info.sampleCount = 1;
    info.width = panel.desc.widthPixels;
    info.height = panel.desc.heightPixels;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (XR_FAILED(xrCreateSwapchain(_session, &info, &panel.swapchain)))
        return false;
    __android_log_print(ANDROID_LOG_INFO, "VRORendererOpenXR",
                        "OpenXR panel swapchains: live=%d (created %ux%u)",
                        ++sLivePanelSwapchains, info.width, info.height);
    uint32_t count = 0;
    if (XR_FAILED(xrEnumerateSwapchainImages(panel.swapchain, 0, &count, nullptr)) || count == 0) {
        destroySwapchain(panel);
        return false;
    }
    panel.images.assign(count, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
    if (XR_FAILED(xrEnumerateSwapchainImages(panel.swapchain, count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(panel.images.data())))) {
        destroySwapchain(panel);
        return false;
    }
    return true;
}

void VROOpenXRPanelCompositor::finishPendingGpuWork() {
    if (_hasPendingGpuWork) {
        // xrDestroySwapchain requires all graphics commands referencing its
        // images to have completed. glFlush alone does not guarantee this.
        glFinish();
        _hasPendingGpuWork = false;
    }
}

void VROOpenXRPanelCompositor::destroySwapchain(Panel &panel) {
    if (panel.swapchain != XR_NULL_HANDLE) {
        finishPendingGpuWork();
        xrDestroySwapchain(panel.swapchain);
        panel.swapchain = XR_NULL_HANDLE;
        __android_log_print(ANDROID_LOG_INFO, "VRORendererOpenXR",
                            "OpenXR panel swapchains: live=%d (destroyed)",
                            --sLivePanelSwapchains);
    }
    panel.images.clear();
    panel.imageState.reset();
}

VROOpenXRPanelError VROOpenXRPanelCompositor::addPanel(
    const VROOpenXRPanelDesc &desc, VROOpenXRPanelProducer producer) {
    if (_session == XR_NULL_HANDLE) return VROOpenXRPanelError::SessionUnavailable;
    if (desc.id.empty()) return VROOpenXRPanelError::EmptyId;
    if (!producer) return VROOpenXRPanelError::MissingProducer;
    const VROOpenXRPanelError invalid = validatePanel(desc);
    if (invalid != VROOpenXRPanelError::None) return invalid;

    Panel panel;
    panel.desc = desc;
    panel.producer = std::move(producer);
    if (!createSwapchain(panel)) return VROOpenXRPanelError::SwapchainFailed;
    _panels.push_back(std::move(panel));
    return VROOpenXRPanelError::None;
}

VROOpenXRPanelError VROOpenXRPanelCompositor::validatePanel(const VROOpenXRPanelDesc &desc) const {
    if (_session == XR_NULL_HANDLE) return VROOpenXRPanelError::SessionUnavailable;
    if (desc.id.empty()) return VROOpenXRPanelError::EmptyId;
    if (find(desc.id)) return VROOpenXRPanelError::DuplicateId;
    if (_panels.size() >= _maxLayers) return VROOpenXRPanelError::LayerBudgetExceeded;
    if (!validPose(desc.pose)) return VROOpenXRPanelError::InvalidPose;
    if (!std::isfinite(desc.widthMeters) || desc.widthMeters <= 0 ||
        !std::isfinite(desc.heightMeters) || desc.heightMeters <= 0)
        return VROOpenXRPanelError::InvalidDimensions;
    if (desc.widthPixels < 16 || desc.heightPixels < 16 ||
        desc.widthPixels > 4096 || desc.heightPixels > 4096)
        return VROOpenXRPanelError::InvalidPixelSize;
    if (desc.shape == VROOpenXRPanelShape::Cylinder) {
        if (!_cylinderEnabled) return VROOpenXRPanelError::CylinderUnavailable;
        if (!std::isfinite(desc.radiusMeters) || desc.radiusMeters <= 0)
            return VROOpenXRPanelError::InvalidCylinderRadius;
        if (desc.widthMeters / desc.radiusMeters >= kFullCircle)
            return VROOpenXRPanelError::InvalidCylinderAngle;
    }
    return VROOpenXRPanelError::None;
}

bool VROOpenXRPanelCompositor::invalidatePanelContent(const std::string &id) {
    auto *panel = find(id);
    if (!panel) return false;
    panel->imageState.invalidate();
    return true;
}

bool VROOpenXRPanelCompositor::updatePanelPose(const std::string &id, const XrPosef &pose) {
    auto *p = find(id);
    if (!p || !validPose(pose)) return false;
    p->desc.pose = pose;
    return true;
}

bool VROOpenXRPanelCompositor::setPanelVisible(const std::string &id, bool visible) {
    auto *p = find(id);
    if (!p) return false;
    p->desc.visible = visible;
    return true;
}

bool VROOpenXRPanelCompositor::removePanel(const std::string &id) {
    auto it = std::find_if(_panels.begin(), _panels.end(),
                           [&id](const Panel &p) { return p.desc.id == id; });
    if (it == _panels.end()) return false;
    destroySwapchain(*it);
    _panels.erase(it);
    return true;
}

void VROOpenXRPanelCompositor::clear() {
    for (auto &panel : _panels) destroySwapchain(panel);
    _panels.clear();
}

void VROOpenXRPanelCompositor::appendLayers(
    XrSpace appSpace, uint32_t remainingLayerSlots, XrDuration predictedDisplayPeriod,
    std::vector<const XrCompositionLayerBaseHeader *> &layers,
    VROOpenXRPanelEvictionReason shortReason) {
    if (appSpace == XR_NULL_HANDLE) return;
    // Presentation changes are reported after the loop, so the listener never
    // runs in the middle of building the layer list.
    std::vector<std::pair<std::string, VROOpenXRPanelPresentation>> changes;
    auto mark = [&changes](Panel &panel, VROOpenXRPanelPresentation presentation) {
        if (panel.presentation != presentation) changes.emplace_back(panel.desc.id, presentation);
    };
    for (auto &panel : _panels) {
        if (!panel.desc.visible || panel.swapchain == XR_NULL_HANDLE) continue;
        if (remainingLayerSlots == 0) {
            // Previously dropped without a word; the host must now show the
            // scene-mesh fallback for it.
            mark(panel, VROOpenXRPanelPresentation::Evicted);
            continue;
        }

        panel.imageState.step(
            [&](uint32_t &index) {
                XrSwapchainImageAcquireInfo acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                return xrAcquireSwapchainImage(panel.swapchain, &acquire, &index) == XR_SUCCESS;
            },
            [&] {
                XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wait.timeout = std::max<XrDuration>(0, predictedDisplayPeriod);
                // XR_TIMEOUT_EXPIRED is a non-successful wait even though it is nonnegative.
                return xrWaitSwapchainImage(panel.swapchain, &wait) == XR_SUCCESS;
            },
            [&](uint32_t index) {
                if (index >= panel.images.size()) return false;
                _hasPendingGpuWork = true; // failed producers may also submit work
                if (!panel.producer(panel.images[index].image,
                                    panel.desc.widthPixels, panel.desc.heightPixels)) return false;
                glFlush();
                return true;
            },
            [&] {
                XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                return xrReleaseSwapchainImage(panel.swapchain, &release) == XR_SUCCESS;
            });

        // The compositor uses the last released image while a new paint is
        // pending; no layer is legal before its first successful release.
        // Such a panel holds no slot and is neither shown nor evicted yet.
        if (!panel.imageState.hasReleasedImage()) continue;
        mark(panel, VROOpenXRPanelPresentation::Compositor);

        XrSwapchainSubImage image = {};
        image.swapchain = panel.swapchain;
        image.imageRect.extent = { (int32_t)panel.desc.widthPixels,
                                   (int32_t)panel.desc.heightPixels };
        image.imageArrayIndex = 0;
        if (panel.desc.shape == VROOpenXRPanelShape::Quad) {
            auto &quad = panel.quad;
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad.space = appSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage = image;
            quad.pose = panel.desc.pose;
            quad.size = { panel.desc.widthMeters, panel.desc.heightMeters };
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad));
        } else {
            auto &cylinder = panel.cylinder;
            cylinder.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            cylinder.space = appSpace;
            cylinder.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            cylinder.subImage = image;
            cylinder.pose = panel.desc.pose;
            cylinder.radius = panel.desc.radiusMeters;
            cylinder.centralAngle = panel.desc.widthMeters / panel.desc.radiusMeters;
            cylinder.aspectRatio = panel.desc.widthMeters / panel.desc.heightMeters;
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&cylinder));
        }
        --remainingLayerSlots;
    }
    for (const auto &change : changes) {
        Panel *panel = find(change.first);
        if (panel == nullptr) continue;  // removed by an earlier listener call
        present(*panel, change.second,
                change.second == VROOpenXRPanelPresentation::Evicted
                    ? shortReason : VROOpenXRPanelEvictionReason::None);
    }
}
