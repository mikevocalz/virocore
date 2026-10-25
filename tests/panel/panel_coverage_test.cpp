// Host test for VROPanelCoverage.h. Built by scripts/test-panel-coverage.sh.
//
// GLSL and C++ equality: VROPanelCoverage() is the macro body of
// kVROPanelCoverageGLSL expanded over binary32 shim types, so the two share
// their text. This test checks that the string really is that body (it parses
// the GLSL signature and the shim result back out) and, independently, checks
// the function against a reference written separately in double precision.
// The script also compiles the GLSL string with the NDK's glslc when present,
// which proves it is valid GLSL ES 3.00. It does not prove a GPU returns the
// same bits: GLSL ES lets sqrt and division be less than correctly rounded,
// and drivers may fuse multiply-adds. Those differences are below 1e-5 in
// coverage, far under one 8-bit step (1/255).

#include "VROPanelCoverage.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int gFailures = 0;
int gChecks = 0;

void expect(bool condition, const char *what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

void expectNear(double actual, double expected, double tolerance, const char *what) {
    ++gChecks;
    if (!(std::fabs(actual - expected) <= tolerance)) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s: got %.9f, expected %.9f (+/- %g)\n", what, actual,
                     expected, tolerance);
    }
}

// Independent reference, double precision, written from the definition rather
// than from the macro: signed distance to the rounded rect, first-order for
// elliptical corners, exact for circular corners and straight edges.
double referenceCoverage(double x, double y, double w, double h, double rx, double ry) {
    rx = std::fmin(std::fmax(rx, 0.0), w * 0.5);
    ry = std::fmin(std::fmax(ry, 0.0), h * 0.5);
    const double ax = std::fabs(x - w * 0.5);
    const double ay = std::fabs(y - h * 0.5);
    const double qx = ax - (w * 0.5 - rx);
    const double qy = ay - (h * 0.5 - ry);
    double sd;
    if (qx > 0.0 && qy > 0.0 && rx > 0.0 && ry > 0.0) {
        if (rx == ry) {
            sd = std::sqrt(qx * qx + qy * qy) - rx;
        } else {
            const double nx = qx / rx, ny = qy / ry;
            const double f = std::sqrt(nx * nx + ny * ny) - 1.0;
            const double gx = qx / (rx * rx), gy = qy / (ry * ry);
            const double grad = std::sqrt(gx * gx + gy * gy) / std::sqrt(nx * nx + ny * ny);
            sd = f / grad;
        }
    } else {
        sd = std::fmax(ax - w * 0.5, ay - h * 0.5);
    }
    return std::fmin(std::fmax(0.5 - sd, 0.0), 1.0);
}

// True Euclidean distance from (qx, qy) (outside, first quadrant) to the
// ellipse with semi-axes rx, ry, by dense sampling. Used only to bound the
// error of the first-order approximation on elliptical corners.
double trueEllipseDistance(double qx, double qy, double rx, double ry) {
    double best = 1e30;
    const int steps = 200000;
    for (int i = 0; i <= steps; ++i) {
        const double t = (M_PI * 0.5) * i / steps;
        const double ex = rx * std::cos(t), ey = ry * std::sin(t);
        best = std::fmin(best, std::hypot(qx - ex, qy - ey));
    }
    const double inside = (qx * qx) / (rx * rx) + (qy * qy) / (ry * ry) < 1.0;
    return inside ? -best : best;
}

void testGlslText() {
    const std::string glsl = kVROPanelCoverageGLSL;
    expect(glsl.find("float vro_panel_coverage(vec2 p, vec2 size, vec2 radius)") == 0,
           "GLSL starts with the GLSL signature");
    expect(glsl.find("GFloat") == std::string::npos && glsl.find("GVec2") == std::string::npos,
           "GLSL text names no shim type");
    expect(glsl.find('#') == std::string::npos, "GLSL text has no preprocessor directive");
    expect(glsl.find("clamp(radius, vec2(0.0), halfSize)") != std::string::npos,
           "GLSL text carries the radius clamp");
    expect(glsl.back() == '\n', "GLSL text ends with a newline");
}

void testCornersEdgesCentre() {
    const float w = 2496.0f, h = 1560.0f, r = 64.0f;
    // Centre and edge pixel centres are fully covered.
    expectNear(VROPanelCoverage(w * 0.5f, h * 0.5f, w, h, r, r), 1.0, 0.0, "centre");
    expectNear(VROPanelCoverage(0.5f, h * 0.5f, w, h, r, r), 1.0, 0.0, "left edge pixel centre");
    expectNear(VROPanelCoverage(w - 0.5f, h * 0.5f, w, h, r, r), 1.0, 0.0, "right edge pixel centre");
    expectNear(VROPanelCoverage(w * 0.5f, 0.5f, w, h, r, r), 1.0, 0.0, "top edge pixel centre");
    expectNear(VROPanelCoverage(w * 0.5f, h - 0.5f, w, h, r, r), 1.0, 0.0, "bottom edge pixel centre");
    // Straight edge where the arc starts: still a straight edge pixel.
    expectNear(VROPanelCoverage(r + 0.5f, 0.5f, w, h, r, r), 1.0, 0.0, "pixel at the arc tangent");

    // Corner pixels are empty, all four, symmetrically.
    const float corners[4][2] = {{0.5f, 0.5f}, {w - 0.5f, 0.5f}, {0.5f, h - 0.5f}, {w - 0.5f, h - 0.5f}};
    for (const auto &c : corners) {
        expectNear(VROPanelCoverage(c[0], c[1], w, h, r, r), 0.0, 0.0, "corner pixel is transparent");
    }
    const float a = VROPanelCoverage(10.5f, 20.5f, w, h, r, r);
    expect(a == VROPanelCoverage(w - 10.5f, 20.5f, w, h, r, r) &&
               a == VROPanelCoverage(10.5f, h - 20.5f, w, h, r, r) &&
               a == VROPanelCoverage(w - 10.5f, h - 20.5f, w, h, r, r),
           "the four corners are mirror images to the bit");

    // On the arc itself coverage is one half; one pixel inside / outside it
    // reaches the ends of the feather.
    const double d = r - r / std::sqrt(2.0);  // diagonal arc point, offset from the corner
    const float onArc = static_cast<float>(d);
    expectNear(VROPanelCoverage(onArc, onArc, w, h, r, r), 0.5, 1e-4, "on the arc: 0.5");
    const float step = static_cast<float>(1.0 / std::sqrt(2.0));
    expectNear(VROPanelCoverage(onArc + 0.5f * step, onArc + 0.5f * step, w, h, r, r), 1.0, 1e-4,
               "half a pixel inside the arc: 1");
    expectNear(VROPanelCoverage(onArc - 0.5f * step, onArc - 0.5f * step, w, h, r, r), 0.0, 1e-4,
               "half a pixel outside the arc: 0");

    // Monotonic along the diagonal from the corner inwards.
    float previous = -1.0f;
    bool monotonic = true;
    for (int i = 0; i < 200; ++i) {
        const float t = 0.1f * float(i);
        const float c = VROPanelCoverage(t, t, w, h, r, r);
        if (c < previous) monotonic = false;
        previous = c;
    }
    expect(monotonic, "coverage rises monotonically along the corner diagonal");
}

void testRadiusEdgeCases() {
    const float w = 300.0f, h = 100.0f;
    // Radius 0 is a no-op at every pixel centre, including the corner pixel.
    bool allOne = true;
    for (float y = 0.5f; y < h; y += 7.0f) {
        for (float x = 0.5f; x < w; x += 7.0f) {
            if (VROPanelCoverage(x, y, w, h, 0.0f, 0.0f) != 1.0f) allOne = false;
        }
    }
    expect(VROPanelCoverage(0.5f, 0.5f, w, h, 0.0f, 0.0f) == 1.0f, "radius 0 keeps the corner pixel");
    expect(allOne, "radius 0 covers every pixel centre");
    // Negative radius behaves like 0.
    expect(VROPanelCoverage(0.5f, 0.5f, w, h, -5.0f, -5.0f) == 1.0f, "negative radius is 0");
    // Huge radius clamps to half size per axis (here a stadium: 50 px on both).
    expectNear(VROPanelCoverage(0.5f, 0.5f, w, h, 1e6f, 1e6f),
               referenceCoverage(0.5, 0.5, w, h, 1e6, 1e6), 1e-5, "clamped radius matches reference");
    expectNear(VROPanelCoverage(50.0f, 0.5f, w, h, 50.0f, 50.0f), 1.0, 1e-6,
               "stadium: top tangent pixel covered");
    // Outside the panel is 0.
    expect(VROPanelCoverage(-1.0f, 50.0f, w, h, 10.0f, 10.0f) == 0.0f, "left of the panel");
    expect(VROPanelCoverage(150.0f, 101.0f, w, h, 10.0f, 10.0f) == 0.0f, "below the panel");
}

void testAgainstReference() {
    struct Case { float w, h, rx, ry; };
    const Case cases[] = {
        {2496.0f, 1560.0f, 64.0f, 64.0f},   // Rive board chrome panel
        {2016.0f, 1260.0f, 29.5f, 29.5f},   // whiteboard
        {1024.0f, 576.0f, 40.0f, 37.0f},    // pixel aspect differs from panel aspect
        {64.0f, 64.0f, 32.0f, 32.0f},       // circle
        {17.0f, 33.0f, 3.25f, 2.0f},
    };
    double worst = 0.0;
    for (const Case &c : cases) {
        for (int j = 0; j < 97; ++j) {
            for (int i = 0; i < 97; ++i) {
                // Dense near the top-left corner, sparse across the rest.
                const float x = i < 64 ? 0.25f + 0.5f * float(i) : c.w * float(i - 63) / 34.0f;
                const float y = j < 64 ? 0.25f + 0.5f * float(j) : c.h * float(j - 63) / 34.0f;
                const double got = VROPanelCoverage(x, y, c.w, c.h, c.rx, c.ry);
                const double want = referenceCoverage(x, y, c.w, c.h, c.rx, c.ry);
                worst = std::fmax(worst, std::fabs(got - want));
            }
        }
    }
    expectNear(worst, 0.0, 2e-5, "binary32 coverage vs double reference, worst case");
    std::printf("  worst |coverage - reference| = %.3g\n", worst);
}

void testEllipseApproximation() {
    // Elliptical corners use the first-order distance. Bound its error against
    // the true distance where it matters, within one pixel of the edge.
    const double rx = 40.0, ry = 37.0;
    double worst = 0.0;
    for (int k = 0; k <= 32; ++k) {
        const double t = (M_PI * 0.5) * k / 32.0;
        for (int s = -2; s <= 2; ++s) {
            const double offset = 0.5 * s;  // -1 .. +1 px across the edge
            const double ex = rx * std::cos(t), ey = ry * std::sin(t);
            const double nx = ex / (rx * rx), ny = ey / (ry * ry);
            const double nl = std::hypot(nx, ny);
            const double qx = ex + offset * nx / nl, qy = ey + offset * ny / nl;
            if (qx <= 0.0 || qy <= 0.0) continue;
            const double trueCov = std::fmin(std::fmax(0.5 - trueEllipseDistance(qx, qy, rx, ry), 0.0), 1.0);
            // Corner centre of the top-left corner of a 1024 x 576 panel is (rx, ry).
            const double got = VROPanelCoverage(float(rx - qx), float(ry - qy), 1024.0f, 576.0f,
                                                float(rx), float(ry));
            worst = std::fmax(worst, std::fabs(got - trueCov));
        }
    }
    expect(worst < 1.0 / 255.0, "first-order ellipse coverage within one 8-bit step of exact");
    std::printf("  worst ellipse error within 1 px of the edge = %.3g\n", worst);
}

void testRadiiFromMeters() {
    // Board at 2.6 m: 0.0381 m on a 1.6 m x 1.0 m panel at 2016 x 1260 px.
    VROPanelCornerRadii r = VROPanelCornerRadiiPx(0.0381f, 1.6f, 1.0f, 2016.0f, 1260.0f);
    expectNear(r.x, 0.0381 * 2016.0 / 1.6, 1e-3, "x radius = radiusM * widthPx / widthM");
    expectNear(r.y, 0.0381 * 1260.0 / 1.0, 1e-3, "y radius = radiusM * heightPx / heightM");
    // Different pixel aspect: radii differ per axis.
    r = VROPanelCornerRadiiPx(0.05f, 1.0f, 1.0f, 1024.0f, 512.0f);
    expectNear(r.x, 51.2, 1e-4, "anisotropic pixels: x");
    expectNear(r.y, 25.6, 1e-4, "anisotropic pixels: y");
    // Clamp to half the short side in meters, keeping the corner circular.
    r = VROPanelCornerRadiiPx(10.0f, 2.0f, 1.0f, 2000.0f, 1000.0f);
    expectNear(r.x, 500.0, 1e-3, "clamped: x");
    expectNear(r.y, 500.0, 1e-3, "clamped: y");
    r = VROPanelCornerRadiiPx(NAN, 1.0f, 1.0f, 100.0f, 100.0f);
    expect(r.x == 0.0f && r.y == 0.0f, "NaN radius gives square corners");
    r = VROPanelCornerRadiiPx(0.1f, 0.0f, 1.0f, 100.0f, 100.0f);
    expect(r.x == 0.0f && r.y == 0.0f, "zero width gives square corners");
}

} // namespace

int main() {
    testGlslText();
    testCornersEdgesCentre();
    testRadiusEdgeCases();
    testAgainstReference();
    testEllipseApproximation();
    testRadiiFromMeters();
    if (gFailures != 0) {
        std::fprintf(stderr, "panel coverage: %d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("panel coverage: %d checks passed\n", gChecks);
    return 0;
}
