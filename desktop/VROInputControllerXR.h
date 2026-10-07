//
//  VROInputControllerXR.h
//
//  Minimal VROInputControllerBase for the desktop OpenXR harness: no
//  controllers, no presenter — satisfies the pure virtuals so a
//  VRORenderer can be constructed and its prepareFrame/renderEye called.
//
//  SOT-KEYWORDS: input, openxr, macos
//

#ifndef VROInputControllerXR_h
#define VROInputControllerXR_h

#include "VROInputControllerBase.h"

class VROInputControllerXR : public VROInputControllerBase {
public:

    VROInputControllerXR(std::shared_ptr<VRODriver> driver) :
        VROInputControllerBase(driver) {}
    virtual ~VROInputControllerXR() {}

    std::string getHeadset() override { return "meta-xr-simulator"; }
    std::string getController() override { return "none"; }

protected:

    VROVector3f getDragForwardOffset() override { return VROVector3f(0, 0, -1); }

    std::shared_ptr<VROInputPresenter> createPresenter(std::shared_ptr<VRODriver> driver) override {
        // Deliberately no presenter: reticle/input event processing is out
        // of milestone scope.
        return nullptr;
    }
};

#endif /* VROInputControllerXR_h */
