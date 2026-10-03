//
//  VROInputControllerWasm.cpp
//  ViroRenderer
//
//  Copyright © 2018 Viro Media. All rights reserved.
//
#include "VROInputControllerWasm.h"
#include "VROProjector.h"
#include "VROInputType.h"

VROVector3f VROInputControllerWasm::getDragForwardOffset() {
    // since the controller is the same as the camera, there's no offset.
    return VROVector3f();
}

void VROInputControllerWasm::onProcess(const VROCamera &camera) {
    _latestCamera = camera;
    updateOrientation(camera);
}

void VROInputControllerWasm::setRenderState(VROMatrix4f view, VROMatrix4f projection,
                                            int viewportWidth, int viewportHeight) {
    _view = view;
    _projection = projection;
    _viewportWidth = viewportWidth;
    _viewportHeight = viewportHeight;
}

VROVector3f VROInputControllerWasm::calculateCameraRay(float x, float y) {
    int viewport[4] = { 0, 0, _viewportWidth, _viewportHeight };
    VROMatrix4f mvp = _projection.multiply(_view);

    // Top-left origin in, as DOM events arrive: VROProjector::unproject flips Y
    // itself, the same convention VROInputControllerAR relies on. Flipping here
    // too cancelled that out and mirrored every tap about the horizontal midline.
    VROVector3f resultNear, resultFar;
    VROProjector::unproject(VROVector3f(x, y, 0), mvp.getArray(), viewport, &resultNear);
    VROProjector::unproject(VROVector3f(x, y, 1), mvp.getArray(), viewport, &resultFar);

    return (resultFar - resultNear).normalize();
}

void VROInputControllerWasm::onScreenTouch(int action, float x, float y) {
    if (_viewportWidth == 0 || _viewportHeight == 0) {
        return;
    }

    if (action == 0 || (action == 1 && _pointerDown)) {
        _pointerDown = true;
        _pointerX = x;
        _pointerY = y;
    }

    VROVector3f ray = calculateCameraRay(x, y);
    VROInputControllerBase::updateHitNode(_latestCamera, _latestCamera.getPosition(), ray);

    // Treat the browser pointer as a controller ray. Updating the input pose on
    // every pointer event seeds drag state on pointer-down, advances the native
    // drag solver on pointer-move, and applies the final position before release.
    // This deliberately reuses VROInputControllerBase's FixedDistance /
    // FixedDistanceOrigin / FixedToPlane behavior instead of maintaining a
    // second JS-only drag implementation.
    VROInputControllerBase::onMove(
        ViroCardBoard::InputSource::Controller,
        _latestCamera.getPosition(),
        _latestCamera.getRotation(),
        ray);

    if (action == 0) {
        VROInputControllerBase::onButtonEvent(ViroCardBoard::ViewerButton,
                                              VROEventDelegate::ClickState::ClickDown);
    } else if (action == 2) {
        VROInputControllerBase::onButtonEvent(ViroCardBoard::ViewerButton,
                                              VROEventDelegate::ClickState::ClickUp);
        _pointerDown = false;
    }
    // action == 1 (move) is handled by onMove above; if the pointer currently
    // owns a VRODraggedObject, VROInputControllerBase::processDragging updates
    // its world transform and emits OnDrag.
}

void VROInputControllerWasm::updateScreenTouch(int touchAction) {
    // Legacy entry point: treat as a touch at the screen center.
    onScreenTouch(touchAction, _viewportWidth / 2.0f, _viewportHeight / 2.0f);
}

void VROInputControllerWasm::updateOrientation(const VROCamera &camera) {
    // Grab controller orientation
    VROQuaternion rotation = camera.getRotation();
    VROVector3f controllerForward = rotation.getMatrix().multiply(kBaseForward);

    // A held pointer owns the ray. Aiming along the camera forward here would
    // feed onMove a screen-center ray every frame and drag the held node there
    // between pointer events.
    if (_pointerDown && _viewportWidth > 0 && _viewportHeight > 0) {
        controllerForward = calculateCameraRay(_pointerX, _pointerY);
    }

    // Perform hit test
    VROInputControllerBase::updateHitNode(camera, camera.getPosition(), controllerForward);

    // Process orientation and update delegates
    VROInputControllerBase::onMove(ViroCardBoard::InputSource::Controller, camera.getPosition(), rotation, controllerForward);
    VROInputControllerBase::processGazeEvent(ViroOculus::InputSource::Controller);
}
