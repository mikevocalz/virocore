//
//  VROPassthroughXR.mm
//

#include "VROPassthroughXR.h"
#include <cstdio>

std::variant<std::unique_ptr<VROPassthroughXR>, std::string>
VROPassthroughXR::start(XrInstance instance, XrSession session, bool extensionEnabled,
                        bool alphaBlendOffered) {
    if (!extensionEnabled) return std::string("XR_FB_passthrough not enabled");
    if (!alphaBlendOffered) return std::string("system does not offer ALPHA_BLEND");

    PFN_xrCreatePassthroughFB create = nullptr;
    PFN_xrCreatePassthroughLayerFB createLayer = nullptr;
    std::unique_ptr<VROPassthroughXR> pt(new VROPassthroughXR());
    xrGetInstanceProcAddr(instance, "xrCreatePassthroughFB", (PFN_xrVoidFunction *)&create);
    xrGetInstanceProcAddr(instance, "xrCreatePassthroughLayerFB",
                          (PFN_xrVoidFunction *)&createLayer);
    xrGetInstanceProcAddr(instance, "xrDestroyPassthroughFB",
                          (PFN_xrVoidFunction *)&pt->_destroyPassthrough);
    xrGetInstanceProcAddr(instance, "xrDestroyPassthroughLayerFB",
                          (PFN_xrVoidFunction *)&pt->_destroyLayer);
    if (!create || !createLayer || !pt->_destroyPassthrough || !pt->_destroyLayer) {
        return std::string("XR_FB_passthrough entry points missing");
    }

    XrPassthroughCreateInfoFB pci{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    pci.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    XrResult r = create(session, &pci, &pt->_passthrough);
    if (XR_FAILED(r)) {
        return "xrCreatePassthroughFB failed: " + std::to_string((int)r);
    }

    XrPassthroughLayerCreateInfoFB lci{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    lci.passthrough = pt->_passthrough;
    lci.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    lci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    r = createLayer(session, &lci, &pt->_layer);
    if (XR_FAILED(r)) {
        return "xrCreatePassthroughLayerFB failed: " + std::to_string((int)r);
    }

    pt->_composition.layerHandle = pt->_layer;
    pt->_composition.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    return pt;
}

VROPassthroughXR::~VROPassthroughXR() {
    if (_layer != XR_NULL_HANDLE) _destroyLayer(_layer);
    if (_passthrough != XR_NULL_HANDLE) _destroyPassthrough(_passthrough);
}

const XrCompositionLayerBaseHeader *VROPassthroughXR::layer() {
    return reinterpret_cast<const XrCompositionLayerBaseHeader *>(&_composition);
}
