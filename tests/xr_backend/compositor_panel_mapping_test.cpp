// Host test for the integer codes RiveCompositorPanel_JNI.cpp sends to
// com.viro.core.RiveCompositorPanel. The Java enums index by these codes, so
// the expected names below are the Java constants in declaration order.
#include "jni/RiveCompositorPanelMapping.h"

#include <cstdio>
#include <cstring>

namespace M = RiveCompositorPanelMapping;

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // RiveCompositorPanel.CreateError
    const VROOpenXRPanelError errors[] = {
        VROOpenXRPanelError::SessionUnavailable,  VROOpenXRPanelError::EmptyId,
        VROOpenXRPanelError::MissingProducer,     VROOpenXRPanelError::DuplicateId,
        VROOpenXRPanelError::LayerBudgetExceeded, VROOpenXRPanelError::InvalidPose,
        VROOpenXRPanelError::InvalidDimensions,   VROOpenXRPanelError::InvalidPixelSize,
        VROOpenXRPanelError::CylinderUnavailable, VROOpenXRPanelError::InvalidCylinderRadius,
        VROOpenXRPanelError::InvalidCylinderAngle, VROOpenXRPanelError::SwapchainFailed,
        VROOpenXRPanelError::ProducerUnavailable,
    };
    for (int i = 0; i < (int) (sizeof(errors) / sizeof(errors[0])); ++i) {
        check(M::createErrorCode(errors[i]) == i, "CreateError order");
    }
    check(M::kInvalidRiveFile == 13, "INVALID_RIVE_FILE follows PRODUCER_UNAVAILABLE");

    // RiveCompositorPanel.Shape
    VROOpenXRPanelShape shape;
    check(M::shapeFromCode(0, &shape) && shape == VROOpenXRPanelShape::Quad, "QUAD");
    check(M::shapeFromCode(1, &shape) && shape == VROOpenXRPanelShape::Cylinder, "CYLINDER");
    check(!M::shapeFromCode(2, &shape) && !M::shapeFromCode(-1, &shape), "shape range");

    // RiveCompositorPanel.Fit
    VRORivePanelFit fit;
    check(M::fitFromCode(0, &fit) && fit == VRORivePanelFit::Fill, "FILL");
    check(M::fitFromCode(1, &fit) && fit == VRORivePanelFit::Contain, "CONTAIN");
    check(M::fitFromCode(6, &fit) && fit == VRORivePanelFit::ScaleDown, "SCALE_DOWN");
    check(!M::fitFromCode(7, &fit) && !M::fitFromCode(-1, &fit), "fit range");

    // RiveCompositorPanel.Presentation, EvictionReason, State
    check(M::presentationCode(VROOpenXRPanelPresentation::Pending) == 0, "PENDING");
    check(M::presentationCode(VROOpenXRPanelPresentation::Compositor) == 1, "COMPOSITOR");
    check(M::presentationCode(VROOpenXRPanelPresentation::Evicted) == 2, "EVICTED");
    check(M::evictionReasonCode(VROOpenXRPanelEvictionReason::None) == 0, "NONE");
    check(M::evictionReasonCode(VROOpenXRPanelEvictionReason::LayerBudget) == 1, "LAYER_BUDGET");
    check(M::evictionReasonCode(VROOpenXRPanelEvictionReason::LayerLimitExceeded) == 2,
          "LAYER_LIMIT_EXCEEDED");
    check(M::stateCode(VRORivePanelState::Initializing) == 0, "INITIALIZING");
    check(M::stateCode(VRORivePanelState::Lost) == 4, "LOST");
    check(M::stateCode(VRORivePanelState::Disposed) == 5, "DISPOSED");

    if (failures) return 1;
    std::printf("compositor panel mapping: all checks passed\n");
    return 0;
}
