#include "VROOpenXRPanelCompositor.h"

#include <algorithm>
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

void VROOpenXRPanelCompositor::destroySwapchain(Panel &panel) {
    if (panel.swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(panel.swapchain);
        panel.swapchain = XR_NULL_HANDLE;
    }
    panel.images.clear();
}

bool VROOpenXRPanelCompositor::addPanel(
    const VROOpenXRPanelDesc &desc, VROOpenXRPanelProducer producer) {
    if (_session == XR_NULL_HANDLE || desc.id.empty() || !producer ||
        find(desc.id) || _panels.size() >= _maxLayers ||
        !validPose(desc.pose) ||
        !std::isfinite(desc.widthMeters) || desc.widthMeters <= 0 ||
        !std::isfinite(desc.heightMeters) || desc.heightMeters <= 0 ||
        desc.widthPixels < 16 || desc.heightPixels < 16 ||
        desc.widthPixels > 4096 || desc.heightPixels > 4096) return false;
    if (desc.shape == VROOpenXRPanelShape::Cylinder) {
        if (!_cylinderEnabled || !std::isfinite(desc.radiusMeters) ||
            desc.radiusMeters <= 0 || desc.widthMeters / desc.radiusMeters >= kFullCircle)
            return false;
    }

    Panel panel;
    panel.desc = desc;
    panel.producer = std::move(producer);
    if (!createSwapchain(panel)) return false;
    _panels.push_back(std::move(panel));
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
    XrSpace appSpace, uint32_t remainingLayerSlots,
    std::vector<const XrCompositionLayerBaseHeader *> &layers) {
    if (appSpace == XR_NULL_HANDLE || remainingLayerSlots == 0) return;
    for (auto &panel : _panels) {
        if (remainingLayerSlots == 0) break;
        if (!panel.desc.visible || panel.swapchain == XR_NULL_HANDLE) continue;

        XrSwapchainImageAcquireInfo acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        uint32_t imageIndex = 0;
        if (XR_FAILED(xrAcquireSwapchainImage(panel.swapchain, &acquire, &imageIndex))) continue;

        XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wait.timeout = XR_INFINITE_DURATION;
        const bool acquired = XR_SUCCEEDED(xrWaitSwapchainImage(panel.swapchain, &wait));
        bool drawn = false;
        if (acquired && imageIndex < panel.images.size()) {
            drawn = panel.producer(panel.images[imageIndex].image,
                                  panel.desc.widthPixels, panel.desc.heightPixels);
            if (drawn) glFlush(); // Submit GLES commands before handing texture to compositor.
        }

        XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        if (XR_FAILED(xrReleaseSwapchainImage(panel.swapchain, &release)) || !drawn) continue;

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
}
