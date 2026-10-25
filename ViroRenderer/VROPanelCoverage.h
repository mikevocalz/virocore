//
//  VROPanelCoverage.h
//  ViroRenderer
//
//  The one rounded-rect coverage function for every panel backend (viro-external
//  ADR 0001, Workstream C). The resolve copy of scene panels, the OpenXR
//  compositor finish pass, the scene-mesh fallback and the Rive pointer hit test
//  all use it; none of them reimplements corners.
//
//  Single source: VRO_PANEL_COVERAGE_BODY is written once, with the float and
//  vec2 type names as parameters. kVROPanelCoverageGLSL stringifies it with
//  GLSL's own types; VROPanelCoverage() expands the same tokens as C++ over
//  VROPanelCoverageShim types whose every operation rounds to binary32, the
//  way GLSL evaluates them. The GLSL text and the C++ function cannot drift
//  apart, because they are the same text.
//
//  Geometry, in pixels:
//    - p is the sample position with the panel spanning [0, size]. Pass pixel
//      centres (i + 0.5, j + 0.5); in a fragment shader that is gl_FragCoord.xy
//      when the viewport covers the whole target.
//    - radius is per axis: radiusM * widthPx / widthM and radiusM * heightPx /
//      heightM (VROPanelCornerRadiiPx). They differ only when the pixel aspect
//      differs from the panel aspect; the corner is then an ellipse in pixels
//      and a circle on the panel.
//    - Each radius is clamped to [0, size / 2] on its axis.
//  The signed distance to the edge is exact on straight edges and on circular
//  corners, and the first-order ellipse distance f / |grad f| on elliptical
//  ones. Coverage is clamp(0.5 - distance, 0, 1): a one-pixel anti-aliased
//  feather centred on the geometric edge, 1.0 at every pixel centre of a
//  straight edge, and 1.0 everywhere when the radius is 0.
//
//  All four corners use the same radius, so the result does not depend on
//  whether rows run top-down or bottom-up.
//
//  GLSL requirements: GLSL ES 1.00 or later, and the including shader must make
//  float default to highp (`precision highp float;`). mediump has too few bits
//  for pixel positions on a 2496 px panel.
//
//  No GL dependency: this header is plain C++11 and is built by the host test
//  scripts/test-panel-coverage.sh.
//

#ifndef VROPanelCoverage_h
#define VROPanelCoverage_h

#include <cmath>

// F is the float type name and V2 the two-component vector type name. Both
// must also be callable as constructors: V2(x) splats, F(x) converts.
#define VRO_PANEL_COVERAGE_BODY(F, V2)                                         \
F vro_panel_coverage(V2 p, V2 size, V2 radius) {                               \
    V2 halfSize = size * 0.5;                                                  \
    V2 r = clamp(radius, V2(0.0), halfSize);                                   \
    V2 q = abs(p - halfSize) - (halfSize - r);                                 \
    F sdEdge = max(q.x - r.x, q.y - r.y);                                      \
    V2 safeR = max(r, V2(1.0e-6));                                             \
    V2 n = q / safeR;                                                          \
    F len = length(n);                                                         \
    F sdCorner = (len - 1.0) * len / max(length(n / safeR), F(1.0e-12));       \
    bool corner = q.x > 0.0 && q.y > 0.0 && r.x > 0.0 && r.y > 0.0;            \
    F sd = corner ? sdCorner : sdEdge;                                         \
    return clamp(0.5 - sd, F(0.0), F(1.0));                                    \
}

#define VRO_PANEL_COVERAGE_STRINGIFY_(...) #__VA_ARGS__
#define VRO_PANEL_COVERAGE_STRINGIFY(...) VRO_PANEL_COVERAGE_STRINGIFY_(__VA_ARGS__)

// GLSL source of `float vro_panel_coverage(vec2 p, vec2 size, vec2 radius)`.
// One line, no preprocessor directives, so it can be pasted anywhere after the
// precision statement of a fragment shader.
static const char *const kVROPanelCoverageGLSL =
    VRO_PANEL_COVERAGE_STRINGIFY(VRO_PANEL_COVERAGE_BODY(float, vec2)) "\n";

namespace VROPanelCoverageShim {

// A float whose arithmetic rounds every operation to binary32 and whose
// literals convert to binary32 first, as GLSL's do. A plain C++ float would
// promote `len - 1.0` to double and keep the extra bits.
struct GFloat {
    float v;
    GFloat(double d) : v(static_cast<float>(d)) {}  // NOLINT: literal conversion, as in GLSL
    GFloat(float f) : v(f) {}                        // NOLINT
};

inline GFloat operator+(GFloat a, GFloat b) { return GFloat(a.v + b.v); }
inline GFloat operator-(GFloat a, GFloat b) { return GFloat(a.v - b.v); }
inline GFloat operator*(GFloat a, GFloat b) { return GFloat(a.v * b.v); }
inline GFloat operator/(GFloat a, GFloat b) { return GFloat(a.v / b.v); }
inline bool operator>(GFloat a, GFloat b) { return a.v > b.v; }

struct GVec2 {
    GFloat x, y;
    explicit GVec2(GFloat s) : x(s), y(s) {}
    GVec2(GFloat a, GFloat b) : x(a), y(b) {}
};

inline GVec2 operator+(GVec2 a, GVec2 b) { return GVec2(a.x + b.x, a.y + b.y); }
inline GVec2 operator-(GVec2 a, GVec2 b) { return GVec2(a.x - b.x, a.y - b.y); }
inline GVec2 operator/(GVec2 a, GVec2 b) { return GVec2(a.x / b.x, a.y / b.y); }
inline GVec2 operator*(GVec2 a, GFloat s) { return GVec2(a.x * s, a.y * s); }

// GLSL ES 1.00 section 8.3/8.4 definitions.
inline GFloat max(GFloat a, GFloat b) { return GFloat(a.v < b.v ? b.v : a.v); }
inline GFloat min(GFloat a, GFloat b) { return GFloat(b.v < a.v ? b.v : a.v); }
inline GFloat clamp(GFloat x, GFloat lo, GFloat hi) { return min(max(x, lo), hi); }
inline GVec2 max(GVec2 a, GVec2 b) { return GVec2(max(a.x, b.x), max(a.y, b.y)); }
inline GVec2 clamp(GVec2 x, GVec2 lo, GVec2 hi) {
    return GVec2(clamp(x.x, lo.x, hi.x), clamp(x.y, lo.y, hi.y));
}
inline GVec2 abs(GVec2 a) { return GVec2(GFloat(std::fabs(a.x.v)), GFloat(std::fabs(a.y.v))); }
inline GFloat length(GVec2 a) {
    const GFloat squared = a.x * a.x + a.y * a.y;
    return GFloat(std::sqrt(squared.v));
}

// Same tokens as kVROPanelCoverageGLSL.
inline VRO_PANEL_COVERAGE_BODY(GFloat, GVec2)

} // namespace VROPanelCoverageShim

/*
 Coverage in [0, 1] of the rounded rect [0, width] x [0, height] at (x, y),
 with per-axis corner radii in pixels. See the file comment for conventions.
 */
inline float VROPanelCoverage(float x, float y, float width, float height,
                              float radiusX, float radiusY) {
    using namespace VROPanelCoverageShim;
    return vro_panel_coverage(GVec2(x, y), GVec2(width, height), GVec2(radiusX, radiusY)).v;
}

struct VROPanelCornerRadii {
    float x = 0.0f;
    float y = 0.0f;
};

/*
 Per-axis pixel radii for a panel with a corner radius in meters. The radius
 is first clamped to half the panel's shorter side in meters (panel-tokens.md
 rule 1), so the corner stays circular on the panel even when it is clamped.
 Non-finite or non-positive input gives radius 0 (square corners).
 */
inline VROPanelCornerRadii VROPanelCornerRadiiPx(float radiusM, float widthM, float heightM,
                                                 float widthPx, float heightPx) {
    VROPanelCornerRadii radii;
    if (!std::isfinite(radiusM) || !std::isfinite(widthM) || !std::isfinite(heightM) ||
        !std::isfinite(widthPx) || !std::isfinite(heightPx) || !(radiusM > 0.0f) ||
        !(widthM > 0.0f) || !(heightM > 0.0f) || !(widthPx > 0.0f) || !(heightPx > 0.0f)) {
        return radii;
    }
    const float shortSideM = widthM < heightM ? widthM : heightM;
    const float r = radiusM < 0.5f * shortSideM ? radiusM : 0.5f * shortSideM;
    radii.x = r * widthPx / widthM;
    radii.y = r * heightPx / heightM;
    return radii;
}

#endif /* VROPanelCoverage_h */
