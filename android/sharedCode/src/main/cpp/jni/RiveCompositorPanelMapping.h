//
//  RiveCompositorPanelMapping.h
//  ViroRenderer
//
//  Integer codes that cross the JNI boundary for com.viro.core.RiveCompositorPanel.
//  Each Java enum is indexed by these values (values()[code]), so the order
//  here and in RiveCompositorPanel.java must match. Free of JNI and GL so the
//  host test (tests/xr_backend/compositor_panel_mapping_test.cpp) can check it.
//

#pragma once

#include "VROOpenXRPanelCompositor.h"
#include "VRORivePanel.h"

#include <cstdint>

namespace RiveCompositorPanelMapping {

// RiveCompositorPanel.CreateError: every VROOpenXRPanelError except None, in
// declaration order, then one code for a .riv the panel refused to queue.
constexpr int32_t kInvalidRiveFile =
    static_cast<int32_t>(VROOpenXRPanelError::ProducerUnavailable);

inline int32_t createErrorCode(VROOpenXRPanelError error) {
    return static_cast<int32_t>(error) - 1;
}

// RiveCompositorPanel.Shape
inline bool shapeFromCode(int32_t code, VROOpenXRPanelShape *out) {
    switch (code) {
        case 0: *out = VROOpenXRPanelShape::Quad; return true;
        case 1: *out = VROOpenXRPanelShape::Cylinder; return true;
        default: return false;
    }
}

// RiveCompositorPanel.Fit, the same order as VRORivePanelFit.
inline bool fitFromCode(int32_t code, VRORivePanelFit *out) {
    if (code < static_cast<int32_t>(VRORivePanelFit::Fill) ||
        code > static_cast<int32_t>(VRORivePanelFit::ScaleDown)) {
        return false;
    }
    *out = static_cast<VRORivePanelFit>(code);
    return true;
}

// RiveCompositorPanel.Presentation
inline int32_t presentationCode(VROOpenXRPanelPresentation presentation) {
    return static_cast<int32_t>(presentation);
}

// RiveCompositorPanel.EvictionReason
inline int32_t evictionReasonCode(VROOpenXRPanelEvictionReason reason) {
    return static_cast<int32_t>(reason);
}

// RiveCompositorPanel.State, the same order as VRORivePanelState.
inline int32_t stateCode(VRORivePanelState state) {
    return static_cast<int32_t>(state);
}

}  // namespace RiveCompositorPanelMapping
