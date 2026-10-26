//
//  VRORivePanelGeometry.h
//  ViroRenderer
//
//  Pure geometry for VRORivePanel: the fit/alignment transform, the rounded
//  panel shape, and per-pointer routing. No rive or GL dependency, so the host
//  unit test (scripts/test-rive-panel-geometry.sh) checks it against
//  rive::computeAlignment built from the vendored source.
//
//  The renderer draws with VRORiveComputeAlignment and pointers are mapped with
//  its inverse, so what is hit is what is drawn.
//

#pragma once

#include "VRORivePanel.h"

#include <array>
#include <cstdint>

// Same layout as rive::Mat2D: (x, y) -> (xx*x + yx*y + tx, xy*x + yy*y + ty).
struct VRORiveAffine {
    float xx = 1.0f, xy = 0.0f, yx = 0.0f, yy = 1.0f, tx = 0.0f, ty = 0.0f;
};

struct VRORiveRect {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    float width() const { return right - left; }
    float height() const { return bottom - top; }
};

// rive::computeAlignment (src/renderer.cpp) for every VRORivePanelFit, with the
// frame at the origin. Returns identity when either rect is empty or a
// size is not finite, where rive would divide by zero.
__attribute__((visibility("hidden")))
VRORiveAffine VRORiveComputeAlignment(VRORivePanelFit fit, float alignmentX, float alignmentY,
                                      float frameWidth, float frameHeight,
                                      const VRORiveRect &content);

// False when the matrix is singular.
__attribute__((visibility("hidden")))
bool VRORiveInvert(const VRORiveAffine &m, VRORiveAffine *out);

__attribute__((visibility("hidden")))
void VRORiveMapPoint(const VRORiveAffine &m, float x, float y, float *outX, float *outY);

// Radius actually used for a w x h panel: clamped to [0, min(w, h) / 2], and 0
// for non-finite input.
__attribute__((visibility("hidden")))
float VRORiveClampCornerRadius(float radiusPx, float width, float height);

// Inside the rounded rect [0, w] x [0, h] with the clamped radius, boundary
// inclusive: VROPanelCoverage (ViroRenderer/VROPanelCoverage.h) >= 0.5, the
// geometric edge where the anti-aliased coverage is one half.
__attribute__((visibility("hidden")))
bool VRORiveRoundedRectContains(float x, float y, float width, float height, float radiusPx);

// Per-axis form for the consumer's radii (VROPanelCornerRadiiPx): each radius
// is clamped on its own axis inside VROPanelCoverage, so the hit shape is
// exactly the drawn shape.
__attribute__((visibility("hidden")))
bool VRORiveRoundedRectContains(float x, float y, float width, float height, float radiusXPx,
                                float radiusYPx);

// The radii pointer hits are tested with: the consumer's coverage radii when
// either is positive, otherwise the producer's own clip radius on both axes.
__attribute__((visibility("hidden")))
void VRORivePanelHitRadii(const VRORivePanelOptions &options, float *radiusXPx, float *radiusYPx);

enum class VRORivePointerPhase : uint8_t { Down, Move, Up };
enum class VRORivePointerAction : uint8_t { Drop, Down, Move, Up, Exit };

// Decides what Rive sees for each pointer event, by pointer id:
//  - Down inside the shape captures the pointer; outside is dropped.
//  - Move while captured is forwarded wherever it lands, so a drag that leaves
//    the shape still ends cleanly. Uncaptured moves inside are hover moves;
//    the first one that leaves the shape becomes a single Exit.
//  - Up releases the capture and is forwarded; an up with no capture is dropped.
// Tracks at most kMaxPointers ids at once; extra ids are dropped.
class __attribute__((visibility("hidden"))) VRORivePointerRouter {
public:
    static constexpr int kMaxPointers = 16;

    VRORivePointerAction route(VRORivePointerPhase phase, bool insideShape, int pointerId);
    // Forget every pointer, for example after the content is replaced.
    void reset();
    bool isCaptured(int pointerId) const;

private:
    struct Slot {
        int id = 0;
        bool used = false;
        bool captured = false;
        bool hovering = false;
    };
    Slot *find(int pointerId);
    const Slot *find(int pointerId) const;
    Slot *findOrAdd(int pointerId);
    std::array<Slot, kMaxPointers> _slots{};
};
