//
//  VROPassthroughXR.h
//
//  XR_FB_passthrough for the desktop OpenXR host: one passthrough handle and
//  one running reconstruction layer, destroyed together. Mirrors the Android
//  path in VROSceneRendererOpenXR.cpp. The layer is composited under the
//  projection layer, which must blend by source alpha for it to show.
//
//  SOT-KEYWORDS: passthrough, openxr, macos, simulator
//

#ifndef VROPassthroughXR_h
#define VROPassthroughXR_h

#include <openxr/openxr.h>
#include <memory>
#include <string>
#include <variant>

class VROPassthroughXR {
public:
    // Creates the passthrough and its layer. Returns the reason in the
    // string alternative when the capability is missing or creation fails;
    // the caller logs it and falls back to immersive rendering.
    static std::variant<std::unique_ptr<VROPassthroughXR>, std::string>
    start(XrInstance instance, XrSession session, bool extensionEnabled,
          bool alphaBlendOffered);

    ~VROPassthroughXR();
    VROPassthroughXR(const VROPassthroughXR &) = delete;
    VROPassthroughXR &operator=(const VROPassthroughXR &) = delete;

    // Composition layer for this frame; valid while this object lives.
    const XrCompositionLayerBaseHeader *layer();

private:
    VROPassthroughXR() = default;

    XrPassthroughFB _passthrough = XR_NULL_HANDLE;
    XrPassthroughLayerFB _layer = XR_NULL_HANDLE;
    PFN_xrDestroyPassthroughFB _destroyPassthrough = nullptr;
    PFN_xrDestroyPassthroughLayerFB _destroyLayer = nullptr;
    XrCompositionLayerPassthroughFB _composition{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
};

#endif /* VROPassthroughXR_h */
