# Basis Universal KTX2 transcoder (transcoder only) plus the Zstandard decoder it
# needs for UASTC levels supercompressed with Zstd. See NOTICE.md for the pinned
# upstream version and licences.
#
# Built as a static library with hidden visibility so neither basisu nor ZSTD_*
# symbols are exported from the renderer's shared library, where they could
# collide with another copy of zstd loaded into the same process.
#
# Linking viro_basisu defines VRO_HAS_BASISU=1 for the consumer, which turns on
# KHR_texture_basisu decoding in VROGLTFLoader (see VROKTX2Texture.h).

add_library(viro_basisu STATIC
    ${CMAKE_CURRENT_LIST_DIR}/transcoder/basisu_transcoder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/zstd/zstddeclib.c)

# basisu needs C++17 (`if constexpr`). The standard set here is emitted after
# any -std in CMAKE_CXX_FLAGS (Android's gradle passes -std=c++14), so it wins.
set_target_properties(viro_basisu PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Only the targets the renderer asks for are compiled in: ASTC 4x4 and ETC2
# RGBA (EAC alpha + ETC1 colour) for GLES, and RGBA32 everywhere. The other
# block formats (BC1-7, PVRTC, ATC, FXT1, EAC R11/RG11) and HDR input are off
# to keep the binary small. Every option here must also be visible to the
# renderer's own sources, which include basisu_transcoder.h, hence PUBLIC.
target_compile_definitions(viro_basisu
    PUBLIC
        VRO_HAS_BASISU=1
        BASISD_SUPPORT_KTX2=1
        BASISD_SUPPORT_KTX2_ZSTD=1
    PRIVATE
        BASISD_SUPPORT_UASTC=1
        BASISD_SUPPORT_ASTC=1
        BASISD_SUPPORT_ETC2_EAC_A8=1
        BASISD_SUPPORT_DXT1=0
        BASISD_SUPPORT_DXT5A=0
        BASISD_SUPPORT_BC7_MODE5=0
        BASISD_SUPPORT_PVRTC1=0
        BASISD_SUPPORT_PVRTC2=0
        BASISD_SUPPORT_ATC=0
        BASISD_SUPPORT_FXT1=0
        BASISD_SUPPORT_ETC2_EAC_RG11=0
        BASISD_SUPPORT_UASTC_HDR=0
        # zstd.h marks its API visibility("default") explicitly, which beats the
        # hidden preset; empty these so ZSTD_* stays internal too.
        "ZSTDLIB_VISIBILITY="
        "ZSTDERRORLIB_VISIBILITY=")

target_include_directories(viro_basisu PUBLIC ${CMAKE_CURRENT_LIST_DIR}/transcoder)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Third-party code: keep its warnings out of the renderer's build logs.
    target_compile_options(viro_basisu PRIVATE -w)
endif()
