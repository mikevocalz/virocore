// VROOpenXRRenderModel.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROOpenXRRenderModel.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unistd.h>

#undef  LOG_TAG
#define LOG_TAG "VROInputOpenXR"
#include "VROOpenXRPlatform.h"

// ──────────────────────────────────────────────────────────────────────────────
// Loader
// ──────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> VROOpenXRRenderModelLoader::load(XrRenderModelKeyFB key,
                                                      XrResult *outResult) {
    std::lock_guard<std::mutex> lock(_mutex);
    XrResult result = XR_ERROR_SESSION_LOST;
    std::vector<uint8_t> bytes;
    if (_session != XR_NULL_HANDLE && _pfnLoad != nullptr) {
        XrRenderModelLoadInfoFB info = { XR_TYPE_RENDER_MODEL_LOAD_INFO_FB };
        info.modelKey = key;
        XrRenderModelBufferFB buffer = { XR_TYPE_RENDER_MODEL_BUFFER_FB };

        // Buffer-size two-call idiom: capacity 0 asks for the size.
        result = _pfnLoad(_session, &info, &buffer);
        if (result == XR_SUCCESS && buffer.bufferCountOutput > 0) {
            bytes.resize(buffer.bufferCountOutput);
            buffer.bufferCapacityInput = buffer.bufferCountOutput;
            buffer.buffer              = bytes.data();
            result = _pfnLoad(_session, &info, &buffer);
            if (result != XR_SUCCESS || buffer.bufferCountOutput == 0) {
                bytes.clear();
            } else {
                bytes.resize(buffer.bufferCountOutput);
            }
        }
    }
    if (outResult) {
        *outResult = result;
    }
    return bytes;
}

void VROOpenXRRenderModelLoader::invalidate() {
    std::lock_guard<std::mutex> lock(_mutex);
    _session = XR_NULL_HANDLE;
    _pfnLoad = nullptr;
}

// ──────────────────────────────────────────────────────────────────────────────
// Path / properties
// ──────────────────────────────────────────────────────────────────────────────

const char *VROOpenXRRenderModels::pathString(Hand hand) {
    return hand == Hand::Left ? "/model_fb/controller/left" : "/model_fb/controller/right";
}

bool VROOpenXRRenderModels::init(XrInstance instance, XrSession session) {
    auto loadFn = [instance](const char *name, PFN_xrVoidFunction *fn) {
        return XR_SUCCEEDED(xrGetInstanceProcAddr(instance, name, fn)) && *fn != nullptr;
    };
    PFN_xrLoadRenderModelFB pfnLoad = nullptr;
    if (!loadFn("xrEnumerateRenderModelPathsFB", (PFN_xrVoidFunction *)&_pfnEnumerate) ||
        !loadFn("xrGetRenderModelPropertiesFB",  (PFN_xrVoidFunction *)&_pfnGetProperties) ||
        !loadFn("xrLoadRenderModelFB",           (PFN_xrVoidFunction *)&pfnLoad)) {
        ALOGW("XR_FB_render_model functions not found; controller render models disabled");
        _pfnEnumerate     = nullptr;
        _pfnGetProperties = nullptr;
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (XR_FAILED(xrStringToPath(instance, pathString(static_cast<Hand>(i)),
                                     &_handPaths[i]))) {
            ALOGW("xrStringToPath(%s) failed; controller render models disabled",
                  pathString(static_cast<Hand>(i)));
            _pfnEnumerate = nullptr;
            return false;
        }
    }
    _session = session;
    _loader  = std::make_shared<VROOpenXRRenderModelLoader>(session, pfnLoad);
    return true;
}

VROOpenXRRenderModels::PathState VROOpenXRRenderModels::pathState(Hand hand) {
    if (_pfnEnumerate == nullptr || _session == XR_NULL_HANDLE) {
        return PathState::NotListed;
    }
    if (!_enumerated) {
        if (_enumerateAttempts >= kMaxEnumerateAttempts) {
            return PathState::NotListed;
        }
        ++_enumerateAttempts;
        uint32_t count = 0;
        XrResult r = _pfnEnumerate(_session, 0, &count, nullptr);
        std::vector<XrRenderModelPathInfoFB> infos;
        if (XR_SUCCEEDED(r) && count > 0) {
            infos.assign(count, XrRenderModelPathInfoFB{ XR_TYPE_RENDER_MODEL_PATH_INFO_FB });
            r = _pfnEnumerate(_session, count, &count, infos.data());
            infos.resize(XR_SUCCEEDED(r) ? count : 0);
        }
        if (XR_FAILED(r)) {
            ALOGW("xrEnumerateRenderModelPathsFB failed: %d (attempt %d/%d)", (int)r,
                  _enumerateAttempts, kMaxEnumerateAttempts);
            return _enumerateAttempts >= kMaxEnumerateAttempts ? PathState::NotListed
                                                               : PathState::Unknown;
        }
        _enumerated = true;
        for (const auto &info : infos) {
            for (int i = 0; i < 2; ++i) {
                if (info.path == _handPaths[i]) {
                    _handListed[i] = true;
                }
            }
        }
        ALOGI("[XR-DIAG] render model paths: %u listed (controller left=%d right=%d)",
              (unsigned)infos.size(), (int)_handListed[0], (int)_handListed[1]);
    }
    return _handListed[static_cast<int>(hand)] ? PathState::Listed : PathState::NotListed;
}

VROOpenXRRenderModels::Properties VROOpenXRRenderModels::getProperties(Hand hand) {
    Properties out;
    if (_pfnGetProperties == nullptr || !_enumerated) {
        out.result = XR_ERROR_CALL_ORDER_INVALID;
        return out;
    }
    // Viro's glTF loader draws multiple meshes, multiple textures and alpha, so
    // both subsets are acceptable; the runtime reports the one it chose in flags.
    XrRenderModelCapabilitiesRequestFB caps = { XR_TYPE_RENDER_MODEL_CAPABILITIES_REQUEST_FB };
    caps.flags = XR_RENDER_MODEL_SUPPORTS_GLTF_2_0_SUBSET_1_BIT_FB |
                 XR_RENDER_MODEL_SUPPORTS_GLTF_2_0_SUBSET_2_BIT_FB;
    XrRenderModelPropertiesFB props = { XR_TYPE_RENDER_MODEL_PROPERTIES_FB };
    props.next = &caps;

    out.result = _pfnGetProperties(_session, _handPaths[static_cast<int>(hand)], &props);
    if (XR_SUCCEEDED(out.result)) {
        out.key     = props.modelKey;
        out.version = props.modelVersion;
        out.flags   = props.flags;
        props.modelName[XR_MAX_RENDER_MODEL_NAME_SIZE_FB - 1] = '\0';
        out.modelName = props.modelName;
    }
    return out;
}

void VROOpenXRRenderModels::invalidate() {
    if (_loader) {
        _loader->invalidate();
    }
    _session = XR_NULL_HANDLE;
}

// ──────────────────────────────────────────────────────────────────────────────
// File helpers
// ──────────────────────────────────────────────────────────────────────────────

std::string VROOpenXRRenderModels::writeModelFile(const std::string &dir,
                                                  XrRenderModelKeyFB key, uint32_t version,
                                                  const std::vector<uint8_t> &bytes) {
    if (dir.empty() || bytes.empty()) {
        return "";
    }
    char name[96];
    snprintf(name, sizeof(name), "/viro_render_model_%llu_v%u.glb",
             (unsigned long long)key, (unsigned)version);
    const std::string finalPath = dir + name;
    // Unique per write: two loads of the same key (both hands, or a retry racing
    // a recheck) must not interleave bytes in one temp file.
    static std::atomic<uint32_t> sWriteCounter{0};
    const std::string tempPath = finalPath + "." + std::to_string(getpid()) + "." +
                                 std::to_string(sWriteCounter.fetch_add(1)) + ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            ALOGW("render model: cannot open %s for writing", tempPath.c_str());
            return "";
        }
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            ALOGW("render model: short write to %s", tempPath.c_str());
            out.close();
            std::remove(tempPath.c_str());
            return "";
        }
    }
    if (std::rename(tempPath.c_str(), finalPath.c_str()) != 0) {
        ALOGW("render model: rename to %s failed", finalPath.c_str());
        std::remove(tempPath.c_str());
        return "";
    }
    return finalPath;
}

bool VROOpenXRRenderModels::usesBasisuTextures(const std::vector<uint8_t> &glb) {
    // GLB: 12-byte header, then chunk 0 = { uint32 length, uint32 type 'JSON', data }.
    constexpr size_t kHeader = 12, kChunkHeader = 8;
    constexpr uint32_t kJsonChunk = 0x4E4F534A;
    if (glb.size() < kHeader + kChunkHeader) {
        return false;
    }
    uint32_t length = 0, type = 0;
    memcpy(&length, glb.data() + kHeader, sizeof(length));
    memcpy(&type,   glb.data() + kHeader + 4, sizeof(type));
    if (type != kJsonChunk || length > glb.size() - kHeader - kChunkHeader) {
        return false;
    }
    static const char kNeedle[] = "KHR_texture_basisu";
    const char *json = reinterpret_cast<const char *>(glb.data() + kHeader + kChunkHeader);
    const char *end  = json + length;
    return std::search(json, end, kNeedle, kNeedle + sizeof(kNeedle) - 1) != end;
}
