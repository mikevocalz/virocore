// Host test for VRORivePanelGeometry. Built by scripts/test-rive-panel-geometry.sh
// together with rive-runtime's own src/renderer.cpp, so the panel's fit and
// alignment transform is compared against rive::computeAlignment itself.

#include "VRORivePanelGeometry.h"

#include "rive/math/aabb.hpp"
#include "rive/math/mat2d.hpp"
#include "rive/math/vec2d.hpp"
#include "rive/renderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

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

bool sameFloat(float a, float b) {
    // Bit-identical, treating +0 and -0 as equal.
    return a == b || (std::isnan(a) && std::isnan(b));
}

bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

rive::Fit toRive(VRORivePanelFit fit) {
    switch (fit) {
        case VRORivePanelFit::Fill: return rive::Fit::fill;
        case VRORivePanelFit::Contain: return rive::Fit::contain;
        case VRORivePanelFit::Cover: return rive::Fit::cover;
        case VRORivePanelFit::FitWidth: return rive::Fit::fitWidth;
        case VRORivePanelFit::FitHeight: return rive::Fit::fitHeight;
        case VRORivePanelFit::None: return rive::Fit::none;
        case VRORivePanelFit::ScaleDown: return rive::Fit::scaleDown;
    }
    return rive::Fit::contain;
}

void testAlignmentMatchesRive() {
    const VRORivePanelFit fits[] = {
        VRORivePanelFit::Fill,     VRORivePanelFit::Contain,   VRORivePanelFit::Cover,
        VRORivePanelFit::FitWidth, VRORivePanelFit::FitHeight, VRORivePanelFit::None,
        VRORivePanelFit::ScaleDown,
    };
    const float alignments[][2] = {
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}, {0.25f, -0.6f},
    };
    // Frames: the 2496x1560 board-chrome panel, a portrait panel, a tiny one.
    const float frames[][2] = {{2496, 1560}, {1008, 630}, {630, 1008}, {37, 91}, {1, 1}};
    // Contents: the BoardChrome artboard, a square, one with a non-zero origin.
    const VRORiveRect contents[] = {
        {0, 0, 2496, 1560}, {0, 0, 1248, 780}, {0, 0, 512, 512}, {-40, 25, 360, 225}, {10, 10, 11, 4000},
    };

    int compared = 0;
    for (VRORivePanelFit fit : fits) {
        for (const auto &a : alignments) {
            for (const auto &f : frames) {
                for (const VRORiveRect &c : contents) {
                    const VRORiveAffine ours =
                        VRORiveComputeAlignment(fit, a[0], a[1], f[0], f[1], c);
                    const rive::Mat2D theirs = rive::computeAlignment(
                        toRive(fit), rive::Alignment(a[0], a[1]), rive::AABB(0, 0, f[0], f[1]),
                        rive::AABB(c.left, c.top, c.right, c.bottom));
                    const bool same = sameFloat(ours.xx, theirs[0]) &&
                                      sameFloat(ours.xy, theirs[1]) &&
                                      sameFloat(ours.yx, theirs[2]) &&
                                      sameFloat(ours.yy, theirs[3]) &&
                                      sameFloat(ours.tx, theirs[4]) &&
                                      sameFloat(ours.ty, theirs[5]);
                    if (!same) {
                        std::fprintf(stderr,
                                     "  fit %d align (%g,%g) frame %gx%g content (%g,%g,%g,%g):\n"
                                     "  ours  %.9g %.9g %.9g %.9g %.9g %.9g\n"
                                     "  rive  %.9g %.9g %.9g %.9g %.9g %.9g\n",
                                     int(fit), a[0], a[1], f[0], f[1], c.left, c.top, c.right,
                                     c.bottom, ours.xx, ours.xy, ours.yx, ours.yy, ours.tx, ours.ty,
                                     theirs[0], theirs[1], theirs[2], theirs[3], theirs[4],
                                     theirs[5]);
                    }
                    expect(same, "VRORiveComputeAlignment is bit-identical to rive::computeAlignment");
                    ++compared;

                    // Pointer mapping: the inverse must agree with rive's own
                    // invertOrIdentity (what CommandServer uses for pointers).
                    VRORiveAffine inverse;
                    expect(VRORiveInvert(ours, &inverse), "alignment matrix is invertible");
                    const rive::Mat2D riveInverse = theirs.invertOrIdentity();
                    const float px[] = {0.0f, f[0] * 0.5f, f[0], f[0] * 0.13f};
                    const float py[] = {0.0f, f[1] * 0.5f, f[1], f[1] * 0.71f};
                    for (int i = 0; i < 4; ++i) {
                        float ax, ay;
                        VRORiveMapPoint(inverse, px[i], py[i], &ax, &ay);
                        const rive::Vec2D r = riveInverse * rive::Vec2D(px[i], py[i]);
                        const float tol = 1e-3f * (1.0f + std::fabs(r.x) + std::fabs(r.y));
                        expect(near(ax, r.x, tol) && near(ay, r.y, tol),
                               "inverse maps panel pixels to the same artboard point as rive");
                        // Round trip back to panel pixels.
                        float bx, by;
                        VRORiveMapPoint(ours, ax, ay, &bx, &by);
                        const float back = 1e-3f * (1.0f + f[0] + f[1]);
                        expect(near(bx, px[i], back) && near(by, py[i], back),
                               "forward(inverse(p)) returns p");
                    }
                }
            }
        }
    }
    expect(compared == 7 * 10 * 5 * 5, "every combination compared");
}

void testAlignmentKnownValues() {
    // Contain a 1248x780 artboard in a 2496x1560 panel: exact 2x, no offset.
    VRORiveAffine m = VRORiveComputeAlignment(VRORivePanelFit::Contain, 0, 0, 2496, 1560,
                                              VRORiveRect{0, 0, 1248, 780});
    expect(m.xx == 2.0f && m.yy == 2.0f && m.tx == 0.0f && m.ty == 0.0f,
           "contain 1248x780 into 2496x1560 is a pure 2x scale");

    // Contain a square in a wide panel, centred: letterbox on the left/right.
    m = VRORiveComputeAlignment(VRORivePanelFit::Contain, 0, 0, 200, 100,
                                VRORiveRect{0, 0, 50, 50});
    expect(m.xx == 2.0f && m.tx == 50.0f && m.ty == 0.0f, "contain square centred");
    float ax, ay;
    VRORiveAffine inv;
    VRORiveInvert(m, &inv);
    VRORiveMapPoint(inv, 50.0f, 0.0f, &ax, &ay);
    expect(ax == 0.0f && ay == 0.0f, "panel (50,0) is the artboard origin");
    VRORiveMapPoint(inv, 10.0f, 50.0f, &ax, &ay);
    expect(ax < 0.0f, "letterbox pixels map outside the artboard");

    // Degenerate input gives identity instead of inf/NaN.
    m = VRORiveComputeAlignment(VRORivePanelFit::Contain, 0, 0, 100, 100, VRORiveRect{0, 0, 0, 10});
    expect(m.xx == 1 && m.yy == 1 && m.tx == 0 && m.ty == 0, "empty content gives identity");
    m = VRORiveComputeAlignment(VRORivePanelFit::Fill, 0, 0, NAN, 100, VRORiveRect{0, 0, 10, 10});
    expect(m.xx == 1 && m.tx == 0, "NaN frame gives identity");
    expect(!VRORiveInvert(VRORiveAffine{0, 0, 0, 0, 0, 0}, &inv), "singular matrix rejected");
}

void testRoundedRect() {
    const float w = 2496, h = 1560, r = 96;
    expect(VRORiveRoundedRectContains(w * 0.5f, h * 0.5f, w, h, r), "centre is inside");
    expect(VRORiveRoundedRectContains(0.0f, h * 0.5f, w, h, r), "left edge midpoint is inside");
    expect(VRORiveRoundedRectContains(w * 0.5f, h, w, h, r), "bottom edge midpoint is inside");
    expect(!VRORiveRoundedRectContains(0.0f, 0.0f, w, h, r), "top-left corner pixel is outside");
    expect(!VRORiveRoundedRectContains(w, 0.0f, w, h, r), "top-right corner pixel is outside");
    expect(!VRORiveRoundedRectContains(0.0f, h, w, h, r), "bottom-left corner pixel is outside");
    expect(!VRORiveRoundedRectContains(w, h, w, h, r), "bottom-right corner pixel is outside");

    // Along the 45-degree diagonal of the top-left corner the boundary sits at
    // r - r/sqrt(2) from each edge.
    const float edge = r - r / std::sqrt(2.0f);
    expect(VRORiveRoundedRectContains(edge + 0.5f, edge + 0.5f, w, h, r), "just inside the arc");
    expect(!VRORiveRoundedRectContains(edge - 0.5f, edge - 0.5f, w, h, r), "just outside the arc");
    // Points on the arc are inside (boundary inclusive).
    expect(VRORiveRoundedRectContains(r, 0.0f, w, h, r), "arc tangent point on the top edge");
    expect(VRORiveRoundedRectContains(0.0f, r, w, h, r), "arc tangent point on the left edge");

    expect(!VRORiveRoundedRectContains(-0.01f, h * 0.5f, w, h, r), "left of the panel is outside");
    expect(!VRORiveRoundedRectContains(w * 0.5f, h + 0.01f, w, h, r), "below the panel is outside");
    expect(!VRORiveRoundedRectContains(NAN, 1.0f, w, h, r), "NaN is outside");
    expect(!VRORiveRoundedRectContains(INFINITY, 1.0f, w, h, r), "inf is outside");

    // No radius: the full rect, corners included.
    expect(VRORiveRoundedRectContains(0.0f, 0.0f, w, h, 0.0f), "radius 0 keeps the corner");
    expect(VRORiveRoundedRectContains(0.0f, 0.0f, w, h, NAN), "NaN radius is treated as 0");

    // Radius larger than half the short side becomes a stadium.
    expect(VRORiveClampCornerRadius(5000.0f, w, h) == h * 0.5f, "radius clamps to half the short side");
    expect(VRORiveRoundedRectContains(h * 0.5f, 0.0f, w, h, 5000.0f), "stadium top tangent");
    expect(!VRORiveRoundedRectContains(1.0f, 1.0f, w, h, 5000.0f), "stadium corner is outside");
    expect(VRORiveClampCornerRadius(-3.0f, w, h) == 0.0f, "negative radius clamps to 0");
}

void testPointerRouter() {
    using P = VRORivePointerPhase;
    using A = VRORivePointerAction;
    VRORivePointerRouter router;

    // Press in a corner never reaches Rive, nor do its move and release.
    expect(router.route(P::Down, false, 1) == A::Drop, "corner down dropped");
    expect(router.route(P::Move, false, 1) == A::Drop, "uncaptured corner move dropped");
    expect(router.route(P::Up, false, 1) == A::Drop, "corner up dropped");
    expect(router.route(P::Up, true, 1) == A::Drop, "up without a captured down dropped");

    // Press inside, drag into a corner, release there: Rive sees the whole drag.
    expect(router.route(P::Down, true, 2) == A::Down, "inside down forwarded");
    expect(router.isCaptured(2), "inside down captures");
    expect(router.route(P::Move, false, 2) == A::Move, "captured move into corner forwarded");
    expect(router.route(P::Up, false, 2) == A::Up, "captured up in corner forwarded");
    expect(!router.isCaptured(2), "up releases capture");
    expect(router.route(P::Move, false, 2) == A::Drop, "released pointer outside is gone");

    // Hover in, hover out: exactly one exit.
    expect(router.route(P::Move, true, 3) == A::Move, "hover move inside forwarded");
    expect(router.route(P::Move, false, 3) == A::Exit, "leaving the shape exits once");
    expect(router.route(P::Move, false, 3) == A::Drop, "further outside moves dropped");

    // Pointers are independent.
    expect(router.route(P::Down, true, 10) == A::Down, "hand pointer down");
    expect(router.route(P::Down, false, 11) == A::Drop, "controller pointer down in corner");
    expect(router.route(P::Move, true, 10) == A::Move, "hand pointer still captured");
    expect(router.route(P::Up, true, 11) == A::Drop, "controller up without capture dropped");
    expect(router.route(P::Up, true, 10) == A::Up, "hand pointer up");

    // Capacity is bounded.
    router.reset();
    for (int i = 0; i < VRORivePointerRouter::kMaxPointers; ++i) {
        expect(router.route(P::Down, true, 100 + i) == A::Down, "within capacity");
    }
    expect(router.route(P::Down, true, 999) == A::Drop, "pointer beyond capacity dropped");
    expect(router.route(P::Up, true, 100) == A::Up, "release frees a slot");
    expect(router.route(P::Move, false, 100) == A::Exit, "hover after up exits on leaving, freeing the slot");
    expect(router.route(P::Down, true, 999) == A::Down, "freed slot reused");

    // reset() forgets captures, so stale ids cannot release into new content.
    router.reset();
    expect(router.route(P::Up, true, 999) == A::Drop, "reset drops captures");
}

} // namespace

int main() {
    testAlignmentMatchesRive();
    testAlignmentKnownValues();
    testRoundedRect();
    testPointerRouter();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("rive_panel_geometry_test: %d checks passed\n", gChecks);
    return 0;
}
