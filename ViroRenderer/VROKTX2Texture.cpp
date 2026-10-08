// VROKTX2Texture.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROKTX2Texture.h"

#if VRO_HAS_BASISU

#include "VRODefines.h"
#include "VROData.h"
#include "VROTexture.h"
// The transcoder headers use `if constexpr`, which the C++14 Android build
// accepts as an extension with a warning per use, and name a GCC-only warning
// group clang does not know. Neither is ours to fix.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++17-extensions"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif
#include "basisu_transcoder.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if VRO_PLATFORM_ANDROID && !VRO_METAL
#include "VROOpenGL.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

namespace {

// Owns a malloc'd block until it is handed to VROData with Move ownership,
// which frees it with free().
struct FreeDeleter {
    void operator()(uint8_t *p) const { free(p); }
};
using MallocBuffer = std::unique_ptr<uint8_t, FreeDeleter>;

void ensureTranscoderInitialized() {
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
}

basist::transcoder_texture_format toBasisFormat(VROKTX2Target target) {
    switch (target) {
        case VROKTX2Target::ASTC4x4:  return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
        case VROKTX2Target::ETC2RGBA: return basist::transcoder_texture_format::cTFETC2_RGBA;
        case VROKTX2Target::RGBA8:    return basist::transcoder_texture_format::cTFRGBA32;
    }
    return basist::transcoder_texture_format::cTFRGBA32;
}

VROTextureFormat toTextureFormat(VROKTX2Target target) {
    switch (target) {
        case VROKTX2Target::ASTC4x4:  return VROTextureFormat::ASTC_4x4_LDR;
        case VROKTX2Target::ETC2RGBA: return VROTextureFormat::ETC2_RGBA8_EAC;
        case VROKTX2Target::RGBA8:    return VROTextureFormat::RGBA8;
    }
    return VROTextureFormat::RGBA8;
}

const char *payloadName(const basist::ktx2_transcoder &ktx2) {
    if (ktx2.is_etc1s()) return "ETC1S";
    if (ktx2.is_uastc()) return "UASTC";
    return "HDR";
}

void setString(std::string *out, const std::string &value) {
    if (out) {
        *out = value;
    }
}

// Returns false when the backend cannot be asked yet (no current GL context),
// so the caller does not cache a guess.
bool detectTarget(VROKTX2Target *outTarget) {
#if VRO_PLATFORM_ANDROID && !VRO_METAL
    // GLES 3.0 guarantees ETC2; ASTC LDR is an extension, present on every
    // Adreno/Mali headset GPU this renderer ships on but checked regardless.
    const char *extensions = (const char *) glGetString(GL_EXTENSIONS);
    if (extensions == nullptr) {
        *outTarget = VROKTX2Target::ETC2RGBA;
        return false;
    }
    *outTarget = strstr(extensions, "GL_KHR_texture_compression_astc_ldr") != nullptr
        ? VROKTX2Target::ASTC4x4 : VROKTX2Target::ETC2RGBA;
    return true;
#else
    // Metal and WebGL substrates upload compressed data without mip levels (and
    // WebGL gates both formats behind extensions), so decode to RGBA8 and let
    // the backend generate mips.
    *outTarget = VROKTX2Target::RGBA8;
    return true;
#endif
}

// No KTX2 texture can have more levels than a 2^31 texel axis allows.
constexpr uint32_t kMaxLevels = 32;

} // namespace

VROKTX2Target VROKTX2Texture::chooseTarget() {
    // -1 until a probe succeeds. A probe without a GL context answers ETC2 (safe
    // on any GLES 3 device) for that call only and is retried on the next.
    static std::atomic<int> cached(-1);
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return (VROKTX2Target) value;
    }
    VROKTX2Target target;
    if (detectTarget(&target)) {
        cached.store((int) target, std::memory_order_release);
    }
    return target;
}

const char *VROKTX2Texture::targetName(VROKTX2Target target) {
    switch (target) {
        case VROKTX2Target::ASTC4x4:  return "ASTC 4x4";
        case VROKTX2Target::ETC2RGBA: return "ETC2 RGBA";
        case VROKTX2Target::RGBA8:    return "RGBA8";
    }
    return "unknown";
}

std::shared_ptr<VROTexture> VROKTX2Texture::createTexture(const std::vector<unsigned char> &bytes,
                                                         bool sRGB, VROKTX2Target target,
                                                         std::string *outSummary,
                                                         std::string *outError) {
    if (bytes.empty()) {
        setString(outError, "image has no bytes");
        return nullptr;
    }
    if (bytes.size() > std::numeric_limits<uint32_t>::max()) {
        setString(outError, "image larger than 4 GB");
        return nullptr;
    }
    ensureTranscoderInitialized();

    // init() keeps a pointer to `bytes`; the transcoder does not outlive this call.
    basist::ktx2_transcoder ktx2;
    if (!ktx2.init(bytes.data(), (uint32_t) bytes.size())) {
        setString(outError, "not a valid KTX2 file");
        return nullptr;
    }
    if (ktx2.get_faces() != 1 || ktx2.get_layers() > 1) {
        setString(outError, "cube maps and texture arrays are not supported");
        return nullptr;
    }
    if (ktx2.is_hdr()) {
        setString(outError, "HDR payloads are not supported");
        return nullptr;
    }
    if (!ktx2.start_transcoding()) {
        setString(outError, "could not read the Basis payload (start_transcoding failed)");
        return nullptr;
    }

    const basist::transcoder_texture_format basisFormat = toBasisFormat(target);
    const uint32_t width  = ktx2.get_width();
    const uint32_t height = ktx2.get_height();

    // RGBA8 decodes level 0 only: every backend can build mips from it, and the
    // GL substrate refuses pregenerated mips for uncompressed data.
    const bool compressed = (target != VROKTX2Target::RGBA8);
    if (ktx2.get_levels() == 0 || ktx2.get_levels() > kMaxLevels) {
        setString(outError, "invalid level count " + std::to_string(ktx2.get_levels()));
        return nullptr;
    }
    const uint32_t levelCount = compressed ? ktx2.get_levels() : 1;

    std::vector<uint32_t> mipSizes;
    mipSizes.reserve(levelCount);
    std::vector<basist::ktx2_image_level_info> levels(levelCount);
    // 64-bit so the sum cannot wrap on 32-bit targets (armv7, wasm32) before
    // the size check below.
    uint64_t totalBytes = 0;
    for (uint32_t level = 0; level < levelCount; ++level) {
        basist::ktx2_image_level_info &info = levels[level];
        if (!ktx2.get_image_level_info(info, level, 0, 0)) {
            setString(outError, "missing level " + std::to_string(level));
            return nullptr;
        }
        // GL derives each level's size from level 0; a file that disagrees
        // would make glCompressedTexImage2D reject the upload.
        const uint32_t expectedWidth  = std::max<uint32_t>(1, width  >> level);
        const uint32_t expectedHeight = std::max<uint32_t>(1, height >> level);
        if (info.m_orig_width != expectedWidth || info.m_orig_height != expectedHeight) {
            setString(outError, "level " + std::to_string(level) + " has unexpected dimensions");
            return nullptr;
        }
        const uint64_t levelBytes = compressed
            ? (uint64_t) info.m_total_blocks * basist::basis_get_bytes_per_block_or_pixel(basisFormat)
            : (uint64_t) info.m_orig_width * info.m_orig_height * 4;
        if (levelBytes > std::numeric_limits<uint32_t>::max()) {
            setString(outError, "level " + std::to_string(level) + " is too large");
            return nullptr;
        }
        mipSizes.push_back((uint32_t) levelBytes);
        totalBytes += levelBytes;
    }
    if (totalBytes > (uint64_t) std::numeric_limits<int>::max()) {
        setString(outError, "decoded texture is too large");
        return nullptr;
    }

    MallocBuffer buffer((uint8_t *) malloc((size_t) totalBytes));
    if (!buffer) {
        setString(outError, "out of memory for " + std::to_string(totalBytes) + " bytes");
        return nullptr;
    }

    size_t offset = 0;
    for (uint32_t level = 0; level < levelCount; ++level) {
        const basist::ktx2_image_level_info &info = levels[level];
        const uint32_t capacity = compressed ? info.m_total_blocks
                                             : info.m_orig_width * info.m_orig_height;
        if (!ktx2.transcode_image_level(level, 0, 0, buffer.get() + offset, capacity, basisFormat)) {
            setString(outError, "transcode of level " + std::to_string(level) + " to " +
                                targetName(target) + " failed");
            return nullptr;
        }
        offset += mipSizes[level];
    }

    std::vector<std::shared_ptr<VROData>> data;
    data.push_back(std::make_shared<VROData>(buffer.release(), (int) totalBytes,
                                             VRODataOwnership::Move));

    VROMipmapMode mipmapMode;
    if (!compressed) {
        mipmapMode = VROMipmapMode::Runtime;
        mipSizes.clear();
    } else if (levelCount > 1) {
        mipmapMode = VROMipmapMode::Pregenerated;
    } else {
        mipmapMode = VROMipmapMode::None;
    }

    std::shared_ptr<VROTexture> texture = std::make_shared<VROTexture>(
        VROTextureType::Texture2D, toTextureFormat(target), VROTextureInternalFormat::RGBA8,
        sRGB, mipmapMode, data, (int) width, (int) height, mipSizes);

    if (outSummary) {
        char line[160];
        snprintf(line, sizeof(line), "%ux%u %s, %u level%s -> %s%s", width, height,
                 payloadName(ktx2), levelCount, levelCount == 1 ? "" : "s",
                 targetName(target), sRGB ? " sRGB" : "");
        *outSummary = line;
    }
    return texture;
}

#endif // VRO_HAS_BASISU
