//
//  VRORivePanelGeometry.cpp
//  ViroRenderer
//

#include "VRORivePanelGeometry.h"

#include "VROPanelCoverage.h"

#include <algorithm>
#include <cmath>

namespace {

// rive::Mat2D::multiply (src/math/mat2d.cpp), same expression shapes so the
// compiler makes the same FMA contraction choices and results stay identical.
VRORiveAffine multiply(const VRORiveAffine &a, const VRORiveAffine &b) {
    float a0 = a.xx, a1 = a.xy, a2 = a.yx, a3 = a.yy, a4 = a.tx, a5 = a.ty, b0 = b.xx, b1 = b.xy,
          b2 = b.yx, b3 = b.yy, b4 = b.tx, b5 = b.ty;
    VRORiveAffine r;
    r.xx = a0 * b0 + a2 * b1;
    r.xy = a1 * b0 + a3 * b1;
    r.yx = a0 * b2 + a2 * b3;
    r.yy = a1 * b2 + a3 * b3;
    r.tx = a0 * b4 + a2 * b5 + a4;
    r.ty = a1 * b4 + a3 * b5 + a5;
    return r;
}

} // namespace

VRORiveAffine VRORiveComputeAlignment(VRORivePanelFit fit, float alignmentX, float alignmentY,
                                      float frameWidth, float frameHeight,
                                      const VRORiveRect &content) {
    const float contentWidth = content.width();
    const float contentHeight = content.height();
    if (!(contentWidth > 0.0f) || !(contentHeight > 0.0f) || !(frameWidth > 0.0f) ||
        !(frameHeight > 0.0f) || !std::isfinite(contentWidth) || !std::isfinite(contentHeight) ||
        !std::isfinite(frameWidth) || !std::isfinite(frameHeight)) {
        return {};
    }
    if (!std::isfinite(alignmentX)) alignmentX = 0.0f;
    if (!std::isfinite(alignmentY)) alignmentY = 0.0f;

    // Operation for operation as in rive::computeAlignment, so results match
    // to the bit: translation * scale(sx, sy) * translate(x, y).
    const float x = -content.left - contentWidth * 0.5f - (alignmentX * contentWidth * 0.5f);
    const float y = -content.top - contentHeight * 0.5f - (alignmentY * contentHeight * 0.5f);

    float scaleX = 1.0f, scaleY = 1.0f;
    switch (fit) {
        case VRORivePanelFit::Fill:
            scaleX = frameWidth / contentWidth;
            scaleY = frameHeight / contentHeight;
            break;
        case VRORivePanelFit::Contain:
            scaleX = scaleY = std::fmin(frameWidth / contentWidth, frameHeight / contentHeight);
            break;
        case VRORivePanelFit::Cover:
            scaleX = scaleY = std::fmax(frameWidth / contentWidth, frameHeight / contentHeight);
            break;
        case VRORivePanelFit::FitHeight:
            scaleX = scaleY = frameHeight / contentHeight;
            break;
        case VRORivePanelFit::FitWidth:
            scaleX = scaleY = frameWidth / contentWidth;
            break;
        case VRORivePanelFit::None:
            scaleX = scaleY = 1.0f;
            break;
        case VRORivePanelFit::ScaleDown: {
            const float minScale = std::fmin(frameWidth / contentWidth, frameHeight / contentHeight);
            scaleX = scaleY = minScale < 1.0f ? minScale : 1.0f;
            break;
        }
    }

    VRORiveAffine translation;
    translation.tx = 0.0f + frameWidth * 0.5f + (alignmentX * frameWidth * 0.5f);
    translation.ty = 0.0f + frameHeight * 0.5f + (alignmentY * frameHeight * 0.5f);
    VRORiveAffine scale;
    scale.xx = scaleX;
    scale.yy = scaleY;
    VRORiveAffine offset;
    offset.tx = x;
    offset.ty = y;
    return multiply(multiply(translation, scale), offset);
}

bool VRORiveInvert(const VRORiveAffine &m, VRORiveAffine *out) {
    const double det = double(m.xx) * m.yy - double(m.xy) * m.yx;
    if (det == 0.0 || !std::isfinite(det)) {
        return false;
    }
    const double inv = 1.0 / det;
    VRORiveAffine r;
    r.xx = float(m.yy * inv);
    r.xy = float(-m.xy * inv);
    r.yx = float(-m.yx * inv);
    r.yy = float(m.xx * inv);
    r.tx = float((double(m.yx) * m.ty - double(m.yy) * m.tx) * inv);
    r.ty = float((double(m.xy) * m.tx - double(m.xx) * m.ty) * inv);
    *out = r;
    return true;
}

void VRORiveMapPoint(const VRORiveAffine &m, float x, float y, float *outX, float *outY) {
    *outX = m.xx * x + m.yx * y + m.tx;
    *outY = m.xy * x + m.yy * y + m.ty;
}

float VRORiveClampCornerRadius(float radiusPx, float width, float height) {
    if (!std::isfinite(radiusPx) || !(radiusPx > 0.0f) || !(width > 0.0f) || !(height > 0.0f)) {
        return 0.0f;
    }
    return std::min(radiusPx, 0.5f * std::min(width, height));
}

bool VRORiveRoundedRectContains(float x, float y, float width, float height, float radiusPx) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || y < 0.0f || x > width ||
        y > height) {
        return false;
    }
    // The shared coverage function decides, so a hit lands exactly where the
    // finish pass and the resolve copy draw the panel: coverage >= 0.5 is the
    // geometric edge and inward. The radius is clamped to half the shorter
    // side first, so the corner stays circular.
    const float r = VRORiveClampCornerRadius(radiusPx, width, height);
    return VROPanelCoverage(x, y, width, height, r, r) >= 0.5f;
}

VRORivePointerRouter::Slot *VRORivePointerRouter::find(int pointerId) {
    for (Slot &slot : _slots) {
        if (slot.used && slot.id == pointerId) return &slot;
    }
    return nullptr;
}

const VRORivePointerRouter::Slot *VRORivePointerRouter::find(int pointerId) const {
    for (const Slot &slot : _slots) {
        if (slot.used && slot.id == pointerId) return &slot;
    }
    return nullptr;
}

VRORivePointerRouter::Slot *VRORivePointerRouter::findOrAdd(int pointerId) {
    if (Slot *slot = find(pointerId)) return slot;
    for (Slot &slot : _slots) {
        if (!slot.used) {
            slot = Slot{pointerId, true, false, false};
            return &slot;
        }
    }
    return nullptr;
}

bool VRORivePointerRouter::isCaptured(int pointerId) const {
    const Slot *slot = find(pointerId);
    return slot != nullptr && slot->captured;
}

void VRORivePointerRouter::reset() { _slots = {}; }

VRORivePointerAction VRORivePointerRouter::route(VRORivePointerPhase phase, bool insideShape,
                                                 int pointerId) {
    Slot *slot = find(pointerId);
    switch (phase) {
        case VRORivePointerPhase::Down: {
            if (slot != nullptr && slot->captured) {
                // A second down without an up: Rive already holds this id.
                return VRORivePointerAction::Move;
            }
            if (!insideShape) {
                if (slot != nullptr && slot->hovering) {
                    slot->hovering = false;
                    slot->used = false;
                    return VRORivePointerAction::Exit;
                }
                return VRORivePointerAction::Drop;
            }
            slot = findOrAdd(pointerId);
            if (slot == nullptr) return VRORivePointerAction::Drop;
            slot->captured = true;
            slot->hovering = true;
            return VRORivePointerAction::Down;
        }
        case VRORivePointerPhase::Move: {
            if (slot != nullptr && slot->captured) {
                return VRORivePointerAction::Move;
            }
            if (insideShape) {
                slot = findOrAdd(pointerId);
                if (slot == nullptr) return VRORivePointerAction::Drop;
                slot->hovering = true;
                return VRORivePointerAction::Move;
            }
            if (slot != nullptr && slot->hovering) {
                slot->used = false;
                return VRORivePointerAction::Exit;
            }
            return VRORivePointerAction::Drop;
        }
        case VRORivePointerPhase::Up: {
            if (slot == nullptr || !slot->captured) {
                return VRORivePointerAction::Drop;
            }
            slot->captured = false;
            if (insideShape) {
                slot->hovering = true;
            } else {
                slot->used = false;
            }
            return VRORivePointerAction::Up;
        }
    }
    return VRORivePointerAction::Drop;
}
