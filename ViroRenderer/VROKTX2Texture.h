// VROKTX2Texture.h
// ViroRenderer
//
// KTX2 images carrying Basis Universal payloads (ETC1S or UASTC, optionally
// Zstandard-supercompressed), as referenced by glTF's KHR_texture_basisu
// extension. The payload is transcoded on the CPU into a format the current
// rendering backend can sample:
//
//   GLES (Android)    ASTC 4x4 when GL_KHR_texture_compression_astc_ldr is
//                     present, else ETC2 RGBA (core in GLES 3.0). Every mip
//                     level stored in the file is transcoded and uploaded.
//   Metal, WebGL,     RGBA8, level 0 only; the backend builds its own mips
//   desktop GL        where it supports runtime mipmapping.
//
// The transcoder lives in ViroRenderer/basisu and is compiled only by builds
// that link its viro_basisu CMake target, which defines VRO_HAS_BASISU=1.
// Other builds (the Xcode projects) get the inline stub below, so callers need
// no platform checks: decoding simply reports that it is unavailable.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef VROKTX2Texture_h
#define VROKTX2Texture_h

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifndef VRO_HAS_BASISU
#define VRO_HAS_BASISU 0
#endif

class VROTexture;

// GPU format a KTX2 payload is transcoded into.
enum class VROKTX2Target {
    ASTC4x4,   // GLES with GL_KHR_texture_compression_astc_ldr
    ETC2RGBA,  // GLES 3.0 baseline
    RGBA8,     // uncompressed fallback (Metal, WebGL, desktop GL)
};

class VROKTX2Texture {
public:
    /*
     True when this build carries the Basis Universal transcoder.
     */
    static bool isAvailable() { return VRO_HAS_BASISU != 0; }

    /*
     The best target the current backend can sample. On GLES this reads the GL
     extension string, so call it on the rendering thread with a current context.
     The answer is computed once and cached for the process.
     */
    static VROKTX2Target chooseTarget();

    static const char *targetName(VROKTX2Target target);

    /*
     Transcode a KTX2 file into a 2D texture. `sRGB` selects the colour space the
     texture is sampled in (glTF: true for baseColor and emissive, false for
     normal, occlusion and metallicRoughness). Arrays, cube maps and HDR payloads
     are rejected.

     On success returns the texture and writes a one-line description of what was
     decoded to `outSummary`. On failure returns nullptr and writes the reason to
     `outError`. Either out pointer may be null.
     */
    static std::shared_ptr<VROTexture> createTexture(const std::vector<unsigned char> &ktx2,
                                                     bool sRGB, VROKTX2Target target,
                                                     std::string *outSummary,
                                                     std::string *outError);
};

#if !VRO_HAS_BASISU
inline VROKTX2Target VROKTX2Texture::chooseTarget() {
    return VROKTX2Target::RGBA8;
}

inline const char *VROKTX2Texture::targetName(VROKTX2Target) {
    return "none";
}

inline std::shared_ptr<VROTexture> VROKTX2Texture::createTexture(const std::vector<unsigned char> &,
                                                                 bool, VROKTX2Target,
                                                                 std::string *,
                                                                 std::string *outError) {
    if (outError) {
        *outError = "this build has no Basis Universal transcoder";
    }
    return nullptr;
}
#endif

#endif /* VROKTX2Texture_h */
