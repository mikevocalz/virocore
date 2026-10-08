// VROOpenXRRenderModel.h
// ViroRenderer
//
// Controller render models supplied by the runtime through XR_FB_render_model
// (Meta Quest). The runtime hands back the GLB of the controller that is
// actually connected (Quest 2 Touch, Quest 3/3S Touch Plus, Quest Pro Touch
// Pro), so the scene shows the real controller instead of a bundled stand-in.
//
// Spec facts this file relies on (OpenXR 1.1, XR_FB_render_model rev 4):
//   - xrEnumerateRenderModelPathsFB must run before xrGetRenderModelPropertiesFB,
//     or the runtime returns XR_ERROR_CALL_ORDER_INVALID.
//   - Controller models live at /model_fb/controller/{left,right} and have their
//     origin at the grip pose.
//   - XrRenderModelCapabilitiesRequestFB on the properties chain declares which
//     glTF subsets the app can draw; without it the runtime assumes subset 1.
//   - XR_RENDER_MODEL_UNAVAILABLE_FB with a null key means "no device right
//     now"; the model can appear later, so the caller retries.
//   - xrLoadRenderModelFB may be slow and should run off time-critical threads.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROOPENXRRENDERMODEL_H
#define ANDROID_VROOPENXRRENDERMODEL_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <openxr/openxr.h>

/*
 Thread-safe handle for xrLoadRenderModelFB. Shared with background loads so a
 load in flight can outlive the controller that started it. invalidate() takes
 the same lock as load(), so once it returns no load touches the session again.
 */
class VROOpenXRRenderModelLoader {
public:
    VROOpenXRRenderModelLoader(XrSession session, PFN_xrLoadRenderModelFB pfnLoad)
        : _session(session), _pfnLoad(pfnLoad) {}

    /*
     Two-call load of the GLB bytes for `key`. Returns an empty vector when the
     model is unavailable, the key no longer matches the connected device, or
     the session was invalidated. `outResult` receives the last XrResult.
     */
    std::vector<uint8_t> load(XrRenderModelKeyFB key, XrResult *outResult);

    /* Blocks until any in-flight load returns. Call before xrDestroySession. */
    void invalidate();

private:
    std::mutex              _mutex;
    XrSession               _session;
    PFN_xrLoadRenderModelFB _pfnLoad;
};

class VROOpenXRRenderModels {
public:
    enum class Hand : int { Left = 0, Right = 1 };

    struct Properties {
        XrResult             result    = XR_ERROR_RUNTIME_FAILURE;
        XrRenderModelKeyFB   key       = XR_NULL_RENDER_MODEL_KEY_FB;
        uint32_t             version   = 0;
        XrRenderModelFlagsFB flags     = 0;
        std::string          modelName;
    };

    /*
     Load the three extension functions. Returns false (and the object stays
     unusable) if any is missing, which happens when the extension was not
     enabled at instance creation.
     */
    bool init(XrInstance instance, XrSession session);

    /* Render-model path for a hand, e.g. "/model_fb/controller/left". */
    static const char *pathString(Hand hand);

    enum class PathState { Listed, NotListed, Unknown };

    /*
     Whether the runtime lists /model_fb/controller/<hand>. Enumerates the paths
     on first use, which also satisfies the call-order rule for getProperties().
     A failed enumeration answers Unknown and is retried on the next call, up to
     kMaxEnumerateAttempts; after that the hand counts as NotListed. Render thread.
     */
    PathState pathState(Hand hand);

    /*
     Properties of the model for the device currently connected to `hand`,
     requesting glTF subset 1 and subset 2. Call pathState() first. Render thread.
     */
    Properties getProperties(Hand hand);

    std::shared_ptr<VROOpenXRRenderModelLoader> loader() const { return _loader; }

    /* Blocks until an in-flight background load returns. */
    void invalidate();

    /*
     Write `bytes` to `<dir>/viro_render_model_<key>_v<version>.glb` through a
     temp file and rename, so a reader never sees a partial GLB. Returns the
     final path, or an empty string on failure.
     */
    static std::string writeModelFile(const std::string &dir, XrRenderModelKeyFB key,
                                      uint32_t version, const std::vector<uint8_t> &bytes);

    /*
     True when the GLB's JSON chunk names KHR_texture_basisu (KTX2 / Basis
     Universal textures; Meta's Touch Plus models use UASTC + Zstd). The caller
     tags its log line with it; VROGLTFLoader logs whether each KTX2 image was
     transcoded or, if not, why the material fell back to its base colour.
     */
    static bool usesBasisuTextures(const std::vector<uint8_t> &glb);

private:
    XrSession _session = XR_NULL_HANDLE;
    XrPath    _handPaths[2] = { XR_NULL_PATH, XR_NULL_PATH };
    bool      _enumerated   = false;   // a successful enumeration happened
    int       _enumerateAttempts = 0;
    static constexpr int kMaxEnumerateAttempts = 3;
    bool      _handListed[2] = { false, false };

    PFN_xrEnumerateRenderModelPathsFB _pfnEnumerate     = nullptr;
    PFN_xrGetRenderModelPropertiesFB  _pfnGetProperties = nullptr;
    std::shared_ptr<VROOpenXRRenderModelLoader> _loader;
};

#endif // ANDROID_VROOPENXRRENDERMODEL_H
