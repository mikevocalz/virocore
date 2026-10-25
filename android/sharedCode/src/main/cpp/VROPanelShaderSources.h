//
//  VROPanelShaderSources.h
//  ViroRenderer
//
//  GLSL for the three passes that put a producer's pixels on a panel, all of
//  them built on the shared coverage function (ViroRenderer/VROPanelCoverage.h):
//
//  - Resolve copy (GLSL ES 1.00): SurfaceTexture OES image -> the mipmapped
//    GL_TEXTURE_2D that scene materials sample (VROAndroidViewTexture).
//  - Panel copy (GLSL ES 3.00): a producer's GL_TEXTURE_2D (the Rive ring) ->
//    an OpenXR compositor swapchain image (the finish pass) or a scene
//    texture (the scene-mesh fallback). Both destinations get the same
//    program, so a panel that falls back from a layer to a mesh looks the same.
//
//  GL-free so scripts/test-panel-shaders.sh can print every variant and compile
//  it with the NDK's glslc on the host.
//
//  Colour, in every pass: producer bytes are sRGB-encoded. A pass whose
//  destination has sRGB storage (or is an sRGB swapchain image) writes linear
//  values and the hardware encodes on write; otherwise it writes the encoded
//  bytes unchanged. Premultiplied input is unpremultiplied before decoding and
//  premultiplied again after, because decode(a * s) != a * decode(s).
//

#pragma once

#include "VROPanelCoverage.h"

#include <string>

// sRGB EOTF (IEC 61966-2-1), the inverse of what GL_SRGB8_ALPHA8 does on write.
static const char *const kVROPanelDecodeSrgbGLSL =
    "vec3 vro_decode_srgb(vec3 c) {\n"
    "    vec3 lower = c / 12.92;\n"
    "    vec3 higher = pow((c + 0.055) / 1.055, vec3(2.4));\n"
    "    return mix(higher, lower, vec3(lessThanEqual(c, vec3(0.04045))));\n"
    "}\n";

// ---------------------------------------------------------------------------
// Resolve copy, GLSL ES 1.00. One triangle covers the target; attribute 0.

static const char *const kVROPanelResolveVertexShader =
    "#version 100\n"
    "attribute vec2 a_position;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    v_texcoord = a_position * 0.5 + 0.5;\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

/*
 Fragment shader of the resolve copy. decodeSrgb is true when the destination
 has sRGB storage. Uniforms: source_texture (unit 0), u_size (target pixels),
 u_radius (per-axis corner radius in pixels, 0 = square), u_premultiplied
 (1.0 when the producer writes premultiplied alpha).

 Straight input: colour decoded (sRGB storage) so the stored bytes equal the
 producer's; coverage scales alpha only.
 Premultiplied input: unpremultiply, decode, premultiply (sRGB storage), then
 coverage scales all four channels.
 */
inline std::string VROPanelResolveFragmentShader(bool decodeSrgb) {
    std::string source = "#version 100\n";
    if (decodeSrgb) {
        source += "#define DECODE_SRGB 1\n";
    }
    source +=
        "#extension GL_OES_EGL_image_external : require\n"
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
        "precision highp float;\n"
        "#else\n"
        "precision mediump float;\n"
        "#endif\n";
    source += kVROPanelCoverageGLSL;
    source += kVROPanelDecodeSrgbGLSL;
    source +=
        "uniform samplerExternalOES source_texture;\n"
        "uniform vec2 u_size;\n"
        "uniform vec2 u_radius;\n"
        "uniform float u_premultiplied;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    vec4 color = texture2D(source_texture, v_texcoord);\n"
        "    float coverage = vro_panel_coverage(gl_FragCoord.xy, u_size, u_radius);\n"
        "    if (u_premultiplied > 0.5) {\n"
        "#ifdef DECODE_SRGB\n"
        "        vec3 straight = color.a > 0.0 ? min(color.rgb / color.a, vec3(1.0)) : vec3(0.0);\n"
        "        color.rgb = vro_decode_srgb(straight) * color.a;\n"
        "#endif\n"
        "        color *= coverage;\n"
        "    } else {\n"
        "#ifdef DECODE_SRGB\n"
        "        color.rgb = vro_decode_srgb(color.rgb);\n"
        "#endif\n"
        "        color.a *= coverage;\n"
        "    }\n"
        "    gl_FragColor = color;\n"
        "}\n";
    return source;
}

// ---------------------------------------------------------------------------
// Panel copy, GLSL ES 3.00. Input is premultiplied, sRGB-encoded, in a
// GL_TEXTURE_2D whose rows are bottom-up (GL convention; Rive's
// TextureRenderTargetGL default), and the destination is bottom-up too
// (OpenXR GLES swapchain images and GL textures), so v_texcoord maps straight
// across. Attribute location 0.

static const char *const kVROPanelCopyVertexShader =
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_position;\n"
    "out vec2 v_texcoord;\n"
    "void main() {\n"
    "    v_texcoord = a_position * 0.5 + 0.5;\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

/*
 Fragment shader of the panel copy. linearOutput is true when the destination
 is GL_SRGB8_ALPHA8: the pass writes linear premultiplied values and the
 hardware encodes them, which is what an OpenXR runtime expects from an sRGB
 swapchain with BLEND_TEXTURE_SOURCE_ALPHA and no UNPREMULTIPLIED_ALPHA. With a
 GL_RGBA8 destination the premultiplied encoded bytes pass through, matching
 how the eye buffers are written in that mode (NonLinear). Coverage scales all
 four channels last. Uniforms: u_source (unit 0), u_size, u_radius.
 */
inline std::string VROPanelCopyFragmentShader(bool linearOutput) {
    std::string source = "#version 300 es\n";
    if (linearOutput) {
        source += "#define LINEAR_OUTPUT 1\n";
    }
    source += "precision highp float;\n";
    source += kVROPanelCoverageGLSL;
    source += kVROPanelDecodeSrgbGLSL;
    source +=
        "uniform sampler2D u_source;\n"
        "uniform vec2 u_size;\n"
        "uniform vec2 u_radius;\n"
        "in vec2 v_texcoord;\n"
        "out vec4 o_color;\n"
        "void main() {\n"
        "    vec4 color = texture(u_source, v_texcoord);\n"
        "#ifdef LINEAR_OUTPUT\n"
        "    vec3 straight = color.a > 0.0 ? min(color.rgb / color.a, vec3(1.0)) : vec3(0.0);\n"
        "    color.rgb = vro_decode_srgb(straight) * color.a;\n"
        "#endif\n"
        "    o_color = color * vro_panel_coverage(gl_FragCoord.xy, u_size, u_radius);\n"
        "}\n";
    return source;
}
