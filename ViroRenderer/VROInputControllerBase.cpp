//
//  VROInputControllerBase.cpp
//  ViroRenderer
//
//  Copyright © 2017 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "VROInputControllerBase.h"
#include "VRODragSamplePolicy.h"
#include "VROTime.h"
#include "VROPortal.h"

static bool sSceneBackgroundAdd = true;

VROInputControllerBase::VROInputControllerBase(std::shared_ptr<VRODriver> driver) :
    _driver(driver) {
    _lastKnownPosition = VROVector3f(0,0,0);
    _lastDraggedNodePosition = VROVector3f(0,0,0);
    _lastClickedNode = nullptr;
    _lastHoveredNode = nullptr;
    _currentPinchedNode = nullptr;
    _currentRotateNode = nullptr;
    _scene = nullptr;
    _currentControllerStatus = VROEventDelegate::ControllerStatus::Unknown;
    
#if VRO_PLATFORM_IOS
    if (kDebugSceneBackgroundDistance) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            debugMoveReticle();
        });
    }
#endif
}

void VROInputControllerBase::debugMoveReticle() {
    if (sSceneBackgroundAdd) {
        kSceneBackgroundDistance += 0.1;
        if (kSceneBackgroundDistance > 20) {
            sSceneBackgroundAdd = false;
        }
    }
    else {
        kSceneBackgroundDistance -= 0.1;
        if (kSceneBackgroundDistance < 0) {
            sSceneBackgroundAdd = true;
        }
    }

#if VRO_PLATFORM_IOS
    pinfo("Background distance is %f", kSceneBackgroundDistance);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        debugMoveReticle();
    });
#endif
}

void VROInputControllerBase::setView(VROMatrix4f view) {
    _view = view;
}

void VROInputControllerBase::setProjection(VROMatrix4f projection) {
    _projection = projection;
}

void VROInputControllerBase::cancelSource(int source) {
    const int ray = rayForSource(source);
    std::vector<std::pair<int, std::shared_ptr<VRONode>>> captured;
    for (auto &entry : _lastClickedNodesBySource) {
        if (rayForSource(entry.first) == ray && entry.second) {
            captured.emplace_back(entry.first, entry.second);
            entry.second = nullptr;
        }
    }
    std::shared_ptr<VRODraggedObject> cancelledDrag = getDraggedObject(ray);
    if (cancelledDrag != nullptr) {
        endDrag(cancelledDrag->_source);
    }
    auto hover = _lastHoveredNodesBySource.find(ray);
    if (hover != _lastHoveredNodesBySource.end() && hover->second) {
        auto node = hover->second;
        hover->second = nullptr;
        if (node->getEventDelegate()) node->getEventDelegate()->onHover(ray, node, false, {});
    }
    _hoverPendingBySource.erase(ray);
    _hoverExitBySource.erase(ray);
    _hitResultsBySource.erase(ray);
    _canvasHoverPositions.erase(ray);
    // Empty coordinates mean off-target release to consumers. Never call the
    // ordinary click-completion path, which can reroute through hover grace.
    for (const auto &entry : captured) {
        for (const auto &delegate : _delegates) {
            delegate->onClick(entry.first, entry.second, VROEventDelegate::ClickUp, {});
        }
        if (entry.second->getEventDelegate()) {
            entry.second->getEventDelegate()->onClick(entry.first, entry.second,
                                                       VROEventDelegate::ClickUp, {});
        }
    }
}

void VROInputControllerBase::onButtonEvent(int source, VROEventDelegate::ClickState clickState) {
    // Resolve the click against the hit of the ray that carries this source
    // (a grip shares its hand's aim ray) so two simultaneous pointers don't
    // clobber each other. Falls back to the legacy `_hitResult` for
    // single-pointer backends.
    int ray = rayForSource(source);
    auto hit = getHitResultForSource(ray);
    bool sourceAware = _hitResultsBySource.count(ray) > 0;
    // Click completion stays per button; hover state belongs to the ray.
    std::shared_ptr<VRONode> &lastClicked = sourceAware
        ? _lastClickedNodesBySource[source]
        : _lastClickedNode;

    // Click capture is keyed by BUTTON source, but OpenXR splits one hand into
    // several — a pinch press on `Controller` released as a fist relax fires
    // its ClickUp on `RightGrip`, which owns no pending click. Without the
    // fallback the up resolves against whatever the ray happens to hit, the
    // capturing quad never sees its release, and the panel's pointer capture
    // stays claimed for the rest of the session. A release therefore also
    // closes any press a sibling source opened on the SAME ray.
    if (clickState == VROEventDelegate::ClickUp && lastClicked == nullptr && sourceAware) {
        for (auto &entry : _lastClickedNodesBySource) {
            if (entry.first != source && rayForSource(entry.first) == ray &&
                entry.second != nullptr) {
                std::vector<float> emptyPos;
                for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
                    delegate->onClick(entry.first, entry.second, clickState, emptyPos);
                }
                if (entry.second->getEventDelegate()) {
                    entry.second->getEventDelegate()->onClick(entry.first, entry.second,
                                                              clickState, emptyPos);
                }
                entry.second = nullptr;
            }
        }
    }

    // A release whose ray missed everything must still close out the press it
    // opened. Returning early here orphaned lastClicked: the node that took
    // ClickDown never saw ClickUp, so Clicked never fired — and on grab-based
    // panels the stuck capture kept their input gate shut for the rest of the
    // session. Deliver the up to lastClicked (empty position, like a
    // background hit), clear it, and end any drag this ray owns.
    if (hit == nullptr) {
        if (clickState == VROEventDelegate::ClickUp && lastClicked != nullptr) {
            std::vector<float> emptyPos;
            for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
                delegate->onClick(source, lastClicked, clickState, emptyPos);
            }
            if (lastClicked->getEventDelegate()) {
                lastClicked->getEventDelegate()->onClick(source, lastClicked, clickState, emptyPos);
            }
            lastClicked = nullptr;
        }
        if (clickState == VROEventDelegate::ClickUp) {
            std::shared_ptr<VRODraggedObject> drag = getDraggedObject(ray);
            if (drag != nullptr) {
                endDrag(drag->_source);
            }
        }
        return;
    }
    std::shared_ptr<VRONode> &lastHovered = sourceAware
        ? _lastHoveredNodesBySource[ray]
        : _lastHoveredNode;
    HoverPending &pending = sourceAware
        ? _hoverPendingBySource[ray]
        : _hoverPending;
    HoverExit &lastExit = sourceAware
        ? _hoverExitBySource[ray]
        : _hoverExit;

    VROVector3f hitLoc = hit->getLocation();
    std::vector<float> pos = {hitLoc.x, hitLoc.y, hitLoc.z};
    if (hit->isBackgroundHit()) {
        pos.clear();
    }

    std::shared_ptr<VRONode> hitNode = hit->getNode();
    std::shared_ptr<VRONode> focusedNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnClick, hitNode);

    // Click grace (see kClickGraceMillis). Compares bubbled handler nodes: the
    // raw hit is a child quad or glyph and differs from the handler even when
    // the ray is squarely on target. A miss onto a node with no hover handler
    // (panel body, background) leaves pending.candidateNode null, so that is
    // deliberately not a condition here.
    double now = VROTimeCurrentMillis();
    std::shared_ptr<VRONode> sticky;
    if (lastHovered != nullptr && pending.startedMillis >= 0 &&
        (now - pending.startedMillis) < kClickGraceMillis) {
        sticky = getNodeToHandleEvent(VROEventDelegate::EventAction::OnClick, lastHovered);
    } else if (focusedNode == nullptr && lastExit.node != nullptr &&
               (now - lastExit.leftMillis) < kClickGraceMillis) {
        sticky = getNodeToHandleEvent(VROEventDelegate::EventAction::OnClick, lastExit.node);
    }
    bool rerouted = sticky != nullptr && sticky != focusedNode;
    if (rerouted) {
        focusedNode = sticky;
        pos.clear();  // the hit position is off the node; payload shape matches a background hit
    }

    // Press capture: ClickUp goes to the node that took this button's
    // ClickDown; Clicked fires only when the release still resolves there.
    bool completed = false;
    if (clickState == VROEventDelegate::ClickUp && lastClicked != nullptr) {
        completed = (focusedNode == lastClicked);
        if (!completed) {
            focusedNode = lastClicked;
            pos.clear();
        }
    }

    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onClick(source, focusedNode, clickState, pos);
    }
    if (focusedNode != nullptr && focusedNode->getEventDelegate()) {
        focusedNode->getEventDelegate()->onClick(source, focusedNode, clickState, pos);
    }

    if (clickState == VROEventDelegate::ClickUp) {
        if (completed) {
            for (std::shared_ptr<VROEventDelegate> delegate : _delegates){
                delegate->onClick(source, focusedNode, VROEventDelegate::ClickState::Clicked, pos);
            }
            if (focusedNode->getEventDelegate()) {
                focusedNode->getEventDelegate()->onClick(source, focusedNode,
                                                         VROEventDelegate::ClickState::Clicked,
                                                         pos);
            }
        }
        lastClicked = nullptr;
        // Only the owning ray ends a drag (grip-start / trigger-release on the
        // same hand still counts); the other hand's release must not drop it,
        // and ends only its own drag, if it has one.
        std::shared_ptr<VRODraggedObject> drag = getDraggedObject(ray);
        if (drag != nullptr) {
            endDrag(drag->_source);
        }
    } else if (clickState == VROEventDelegate::ClickDown){
        lastClicked = focusedNode;

        // A second button on a ray that is already dragging neither restarts
        // its drag (the frozen hit would re-seed it with a stale offset) nor
        // starts a second one. A single-pointer drag blocks every source, as
        // the single drag slot did.
        if (getDraggedObject(ray) != nullptr) {
            return;
        }
        // A button with no ray of its own (e.g. a shared BackButton) resolves
        // against the legacy hit and would start an unowned drag, which every
        // ray moves. Never let one start alongside another drag.
        if (!sourceAware && isDragging()) {
            return;
        }

        // Identify if object is draggable.
        std::shared_ptr<VRONode> draggableNode
                = getNodeToHandleEvent(VROEventDelegate::EventAction::OnDrag,
                                       rerouted ? focusedNode : hitNode);
        
        if (draggableNode == nullptr){
            return;
        }

        // One node follows one ray: a second hand grabbing a node the first is
        // already dragging neither steals it nor starts a competing drag. The
        // same goes for an ancestor or descendant of a dragged node, since both
        // drags would write the same subtree's world transform every frame.
        for (const auto &entry : _draggedObjects) {
            std::shared_ptr<VRONode> other = entry.second->_draggedNode;
            if (isSameOrAncestor(other, draggableNode) || isSameOrAncestor(draggableNode, other)) {
                return;
            }
        }

        // A press on a clickable node inside a draggable one (a button on a
        // panel) is a click, not a drag: moving the panel under the ray made
        // the release miss the button, so Clicked never fired.
        if (focusedNode != nullptr && focusedNode != draggableNode) {
            for (std::shared_ptr<VRONode> n = focusedNode->getParentNode(); n != nullptr;
                 n = n->getParentNode()) {
                if (n == draggableNode) {
                    return;
                }
            }
        }

        /*
         Grab and save a reference to the draggedNode that we will be tracking.
         Grab and save the distance of the hit result from the controller.
         Grab and save the hit location from the hit test and original draggedNode position.
         For each of the above, store them within the drag's entry to be used later
         within onMove to calculate the new dragged location of the draggedNode
         in reference to the controller's movement.
         */
        std::shared_ptr<VRODraggedObject> draggedObject = std::make_shared<VRODraggedObject>();
        draggedObject->_originalDraggedNodePosition = draggableNode->getWorldPosition();
        draggedObject->_originalDraggedNodeRotation = draggableNode->getWorldRotation();
        draggedObject->_draggedNode = draggableNode;
        switch (draggableNode->getDragTransform()) {
            case VRODragTransform::Self: draggedObject->_transformNode = draggableNode; break;
            case VRODragTransform::Parent: draggedObject->_transformNode = draggableNode->getParentNode(); break;
            case VRODragTransform::None: break;
        }
        if (draggedObject->_transformNode) {
            draggedObject->_transformOffset = draggedObject->_transformNode->getWorldPosition()
                    - draggableNode->getWorldPosition();
            draggedObject->_transformRotation = draggedObject->_transformNode->getWorldRotation();
            draggedObject->_transformNode->setIsBeingDragged(true);
        }
        // Snapshot this ray's hit now: by the time the owning onMove reaches
        // processDragging, the shared _hitResult may be the other hand's. A
        // re-routed click's hit is off the node (possibly the far background),
        // so anchor that case at the node instead.
        draggedObject->_originalHitLocation = rerouted ? draggableNode->getWorldPosition()
                                                       : hit->getLocation();
        draggedObject->_source = sourceAware ? ray : kUnownedSource;
        // Seed from the owning ray's own pose; _lastKnown* holds whichever
        // source moved last this frame. onMove refreshes both before the first
        // processDragging, so this only matters until then.
        auto pose = _lastKnownPoseBySource.find(ray);
        if (sourceAware && pose != _lastKnownPoseBySource.end()) {
            draggedObject->_position = pose->second.position;
            draggedObject->_forward = pose->second.forward;
        } else {
            draggedObject->_position = _lastKnownPosition;
            draggedObject->_forward = _lastKnownForward;
        }
        draggedObject->_lastNotifiedPosition = _lastDraggedNodePosition;
        draggedObject->_dragState = VROEventDelegate::DragState::Start;
        _draggedObjects[draggedObject->_source] = draggedObject;
        draggableNode->setIsBeingDragged(true);
    }
}

void VROInputControllerBase::onTouchpadEvent(int source, VROEventDelegate::TouchState touchState,
                                             float posX,
                                             float posY) {
    // Avoid spamming similar TouchDownMove events.
    VROVector3f currentTouchedPosition = VROVector3f(posX, posY, 0);
    if (touchState == VROEventDelegate::TouchState::TouchDownMove &&
        _lastTouchedPosition.isEqual(currentTouchedPosition)) {
        return;
    }
    _lastTouchedPosition = currentTouchedPosition;

    std::shared_ptr<VRONode> focusedNode;
    if (_hitResult) {
        focusedNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnTouch, _hitResult->getNode());
    }
    
    // Notify internal delegates
    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onTouch(source, focusedNode, touchState, posX, posY);
    }
    if (focusedNode != nullptr) {
        focusedNode->getEventDelegate()->onTouch(source, focusedNode, touchState, posX, posY);
    }
}

void VROInputControllerBase::onMove(int source, VROVector3f position, VROQuaternion rotation, VROVector3f forward) {
    _lastKnownRotation = rotation;
    _lastKnownPosition = position;
    _lastKnownForward = forward;
    _lastKnownPoseBySource[source] = { position, forward };
    // This source's own hit, not the shared one: with one hand dragging, the shared hit
    // follows the other hand, and fuse and onMove would go to what that hand points at.
    // Single-pointer backends have no per-source hits and get the shared one as before.
    std::shared_ptr<VROHitTestResult> hit = getHitResultForSource(source);
    if (hit == nullptr) {
        return;
    }

    // Trigger orientation delegate callbacks within the scene.
    processOnFuseEvent(source, hit->getNode());

    std::shared_ptr<VRONode> movableNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnMove,
                                                                hit->getNode());
    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onMove(source, movableNode, _lastKnownRotation.toEuler(), _lastKnownPosition, _lastKnownForward);
    }
    if (movableNode != nullptr) {
        movableNode->getEventDelegate()->onMove(source, movableNode, _lastKnownRotation.toEuler(),
                                                _lastKnownPosition, _lastKnownForward);
    }
    
    // Update draggable objects if needed unless we have a pinch motion. Each
    // ray moves only the drag it owns, so with two hands tracked each hand's
    // onMove drives its own node and never the other hand's.
    if ((_currentPinchedNode == nullptr) && (_currentRotateNode == nullptr)) {
        std::shared_ptr<VRODraggedObject> drag = getDraggedObject(source);
        if (drag != nullptr) {
            drag->_position = position;
            drag->_forward = forward;
            processDragging(source);
        }
    }
}

bool VROInputControllerBase::isSameOrAncestor(const std::shared_ptr<VRONode> &ancestor,
                                              std::shared_ptr<VRONode> node) {
    while (node != nullptr) {
        if (node == ancestor) {
            return true;
        }
        node = node->getParentNode();
    }
    return false;
}

std::shared_ptr<VROInputControllerBase::VRODraggedObject>
VROInputControllerBase::getDraggedObject(int ray) const {
    auto it = _draggedObjects.find(ray);
    if (it != _draggedObjects.end()) {
        return it->second;
    }
    // Copied to a local: find() binds a reference, which would odr-use the
    // static constexpr member, and under C++14 it has no out-of-line definition.
    const int unowned = kUnownedSource;
    it = _draggedObjects.find(unowned);
    if (it != _draggedObjects.end()) {
        return it->second;
    }
    return nullptr;
}

void VROInputControllerBase::endDrag(int key) {
    auto it = _draggedObjects.find(key);
    if (it == _draggedObjects.end()) {
        return;
    }
    std::shared_ptr<VRODraggedObject> drag = it->second;
    _draggedObjects.erase(it);
    drag->_dragState = VROEventDelegate::DragState::End;
    if (drag->_draggedNode != nullptr) {
        drag->_draggedNode->setIsBeingDragged(false);
    }
    if (drag->_transformNode != nullptr) {
        drag->_transformNode->setIsBeingDragged(false);
    }
}

void VROInputControllerBase::endAllDrags() {
    while (!_draggedObjects.empty()) {
        endDrag(_draggedObjects.begin()->first);
    }
}

void VROInputControllerBase::applyDragTransform(const std::shared_ptr<VRODraggedObject> &drag,
                                                VROVector3f position,
                                                bool animated) {
    if (drag && drag->_transformNode) {
        drag->_transformNode->setWorldTransform(
                position + drag->_transformOffset,
                drag->_transformRotation, animated);
    }
}

void VROInputControllerBase::processDragging(int source) {
    std::shared_ptr<VRODraggedObject> drag = getDraggedObject(source);
    if (drag == nullptr) {
        return;
    }
    std::shared_ptr<VRONode> draggedNode = drag->_draggedNode;

    // The node can leave the tree mid-drag: a scene change or a remount detaches it while the
    // hand is still holding it. Its world transform has no parent to be relative to then
    // (setWorldTransform dereferenced a null parent on Quest), so the drag ends here.
    if (draggedNode == nullptr || draggedNode->getParentNode() == nullptr) {
        endDrag(drag->_source);
        return;
    }

    // Calculate starting pre-drag properties if needed (hit locations, offsets, etc).
    if (drag->_dragState == VROEventDelegate::DragState::Start) {
        if (draggedNode->getDragType() == VRODragType::FixedDistanceOrigin) {
            drag->_draggedDistanceFromController = draggedNode->getWorldPosition().distanceAccurate(drag->_position);
            drag->_originalHitLocation = draggedNode->getWorldPosition();
        } else if (draggedNode->getDragType() == VRODragType::FixedToPlane) {
            drag->_originalHitLocation = getPlaneIntersect(draggedNode, drag);

            // Snap object onto the plane if need be. This is a tolerance rather
            // than floorf(x * 100) / 100, which sent every value in [-0.01, 0)
            // to -0.01, so float noise on a node that is on the plane read as
            // off it and snapped the node to the aim point.
            VROVector3f p = draggedNode->getDragPlanePoint();
            VROVector3f n = draggedNode->getDragPlaneNormal();
            VROVector3f c = draggedNode->getWorldPosition();
            float distanceFromPlane = (n.x * (c.x - p.x)) + (n.y * (c.y - p.y)) + (n.z * (c.z - p.z));
            if (fabs(distanceFromPlane) > ON_PLANE_DISTANCE_THRESHOLD){
                drag->_originalDraggedNodePosition = drag->_originalHitLocation;
            }
        } else {
            // _originalHitLocation was snapshotted from the owning ray at ClickDown.
            drag->_draggedDistanceFromController = drag->_originalHitLocation.distanceAccurate(drag->_position);
        }

        // Grab the forwardOffset (delta from the controller's forward in reference to the user).
        drag->_forwardOffset = getDragForwardOffset();
        drag->_dragState = VROEventDelegate::DragState::Dragging;
    }

    // Only calculate drag-to node positions for nodes in dragging states.
    if (drag->_dragState != VROEventDelegate::DragState::Dragging) {
        return;
    }

    /*
     * Calculate the new drag-to world position based on the DragType for this node.
     */
    VROVector3f draggedToPosition;
    switch(draggedNode->getDragType()) {
        case VRODragType::FixedToPlane:
            draggedToPosition = getDragPositionFixedToPlane(drag);
            break;
        case VRODragType::FixedToWorld: // this is only supported in AR, so default to FixedDistance here
        case VRODragType::FixedDistance:
        case VRODragType::FixedDistanceOrigin:
            draggedToPosition = getDragPositionFixedDistance(drag);
            break;
    }

    applyDragTransform(drag, draggedToPosition);

    /*
     To avoid spamming the JNI / JS bridge, throttle the notification
     of onDrag delegates to a certain degree of accuracy.
     */
    float distance = draggedToPosition.distance(drag->_lastNotifiedPosition);
    const bool precisionSurface = draggedNode->getHighAccuracyEvents() &&
                                  draggedNode->getDragTransform() == VRODragTransform::None;
    if (!VROShouldEmitDragSample(distance, precisionSurface, ON_DRAG_DISTANCE_THRESHOLD)) {
        return;
    }

    // Update last known dragged position and notify delegates
    drag->_lastNotifiedPosition = draggedToPosition;
    _lastDraggedNodePosition = draggedToPosition;
    if (draggedNode != nullptr && draggedNode->getEventDelegate()) {
        draggedNode->getEventDelegate()->onDrag(source, draggedNode, draggedToPosition);
    }
    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onDrag(source, draggedNode, draggedToPosition);
    }
}

VROVector3f VROInputControllerBase::getDragPositionFixedDistance(const std::shared_ptr<VRODraggedObject> &drag) {
    // This is the forward plus the offset from the camera to the controller
    VROVector3f adjustedForward = drag->_forward + drag->_forwardOffset;

    // controller position + adjustedForward scaled by the distanceFromController (to maintain fixed distance)
    VROVector3f dragPositionWorld = drag->_position + (adjustedForward * drag->_draggedDistanceFromController);

    // The offset is the new drag location minus the original HitTest location
    VROVector3f draggedOffset = dragPositionWorld - drag->_originalHitLocation;

    // Finally, the position returned is the "starting" position of the dragged object + the offset.
    return drag->_originalDraggedNodePosition + draggedOffset;
}

VROVector3f VROInputControllerBase::getPlaneIntersect(std::shared_ptr<VRONode> node,
                                                      const std::shared_ptr<VRODraggedObject> &drag) {
    // get the information from the node (set by the dev)
    VROVector3f planePoint = node->getDragPlanePoint();
    VROVector3f planeNormal = node->getDragPlaneNormal();
    float maxDistance = node->getDragMaxDistance();

    // if the plane info hasn't been set, then just leave the node where it is.
    // Every exit from this function is applied by the caller as a world
    // transform, so it has to be a world position: getPosition() is the offset
    // inside the parent, which for an anchored node moved it by the anchor's
    // own translation.
    if (planeNormal.isZero()) { // we don't check if planePoint is zero because that's a "valid" point
        return node->getWorldPosition();
    }

    // Find the intersection between the plane and the controller forward
    VROVector3f intersectionPoint;
    bool success = drag->_forward.rayIntersectPlane(planePoint, planeNormal,
                                                       drag->_position, &intersectionPoint);

    // if there wasn't an intersection point OR the intersectionPoint was too far from the controller's
    // position, then we want to compute the plane position at maxDistance from the controller's
    // position along the controller's forward. (this is the circle you get b/t intersection of a
    // sphere and a plane).
    if (!success || intersectionPoint.distance(drag->_position) > maxDistance) {

        // first, project the controller's position onto the plane
        VROVector3f controllerProj;
        success = drag->_position.projectOnPlane(planePoint, planeNormal, &controllerProj);
        if (!success) {
            return node->getWorldPosition();
        }

        // second, project the controller's position + forward onto the plane
        VROVector3f forwardProj;
        success = drag->_position.add(drag->_forward).projectOnPlane(planePoint, planeNormal, &forwardProj);
        if (!success) {
            return node->getWorldPosition();
        }

        // A sphere of radius maxDistance around the controller only reaches the
        // plane while the controller is inside that distance of it. Further out
        // the two constraints share no point and the root below is of a negative
        // number, and a NaN reaches the caller as a world transform and takes the
        // node off screen. distanceAccurate because distance() returns NaN for
        // two identical points, which is a controller sitting on the plane.
        float distanceToPlane = drag->_position.distanceAccurate(controllerProj);
        if (distanceToPlane > maxDistance) {
            return node->getWorldPosition();
        }

        // find the length of the 3rd side of the right handed triangle formed by the controller's
        // position, it's projected point, and the position on the plane "maxDistance" from the controller
        float length = sqrtf(powf(maxDistance, 2) - powf(distanceToPlane, 2));

        // finally, calculate the intersection point b/t the plane and sphere along the user's forward.
        // Aiming straight away from the plane leaves no in-plane direction and
        // normalize() would divide by sqrtf(0).
        VROVector3f inPlaneAim = forwardProj.subtract(controllerProj);
        if (inPlaneAim.magnitude() < MIN_DRAG_AIM_IN_PLANE) {
            return node->getWorldPosition();
        }
        intersectionPoint = controllerProj.add(inPlaneAim.normalize().scale(length));
    }

    return intersectionPoint;
}

VROVector3f VROInputControllerBase::getDragPositionFixedToPlane(const std::shared_ptr<VRODraggedObject> &drag) {
    // The offset is the intersectionPoint minus the original HitTest location
    std::shared_ptr<VRONode> node = drag->_draggedNode;
    VROVector3f draggedOffset = getPlaneIntersect(node, drag) - drag->_originalHitLocation;

    // Finally, the position returned is the "starting" position of the dragged object + the offset.
    // This positions the dragged node relative to its parent.
    return drag->_originalDraggedNodePosition + draggedOffset;
}

void VROInputControllerBase::onPinch(int source, float scaleFactor, VROEventDelegate::PinchState pinchState) {
    if(pinchState == VROEventDelegate::PinchState::PinchStart) {
        if(_hitResult == nullptr) {
            return;
        }
        _lastPinchScale = scaleFactor;
        _currentPinchedNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnPinch, _hitResult->getNode());
    }
    
    if(_currentPinchedNode && pinchState == VROEventDelegate::PinchState::PinchMove) {
        if(fabs(scaleFactor - _lastPinchScale) < ON_PINCH_SCALE_THRESHOLD) {
            return;
        }
    }

    if(_currentPinchedNode && _currentPinchedNode->getEventDelegate()) {
        _currentPinchedNode->getEventDelegate()->onPinch(source, _currentPinchedNode, scaleFactor, pinchState);
        if(pinchState == VROEventDelegate::PinchState::PinchEnd) {
            _currentPinchedNode = nullptr;
        }
    }
}

void VROInputControllerBase::onRotate(int source, float rotationRadians, VROEventDelegate::RotateState rotateState) {
    if(rotateState == VROEventDelegate::RotateState::RotateStart) {
        if(_hitResult == nullptr) {
            return;
        }
        _lastRotation = rotationRadians;
        _currentRotateNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnRotate, _hitResult->getNode());
    }
    
    if(_currentRotateNode && rotateState == VROEventDelegate::RotateState::RotateMove) {
        if(fabs(rotationRadians - _lastRotation) < ON_ROTATE_THRESHOLD) {
            return;
        }
    }
    
    if(_currentRotateNode && _currentRotateNode->getEventDelegate()) {
        _currentRotateNode->getEventDelegate()->onRotate(source, _currentRotateNode, rotationRadians, rotateState);
        if(rotateState == VROEventDelegate::RotateState::RotateEnd) {
            _currentRotateNode = nullptr;
        }
    }
}

void VROInputControllerBase::updateHitNode(const VROCamera &camera, VROVector3f origin, VROVector3f ray) {
    // Input-only panel drags keep their collider stationary and continue hit
    // testing; moving drags keep their captured hit.
    std::shared_ptr<VRODraggedObject> drag = getDraggedObject(kUnownedSource);
    if (_scene == nullptr || (drag != nullptr && drag->_transformNode != nullptr)) {
        return;
    }

    _hitResult = std::make_shared<VROHitTestResult>(hitTest(camera, origin, ray, true));
}

void VROInputControllerBase::updateHitNode(int source, const VROCamera &camera,
                                           VROVector3f origin, VROVector3f ray,
                                           bool mirrorToLegacy) {
    // Freeze only this ray's moving drag. A second hand still hit-tests, while
    // an input-only Rive/Canvas drag (no transform node) keeps reporting pointer
    // coordinates as the ray moves.
    std::shared_ptr<VRODraggedObject> drag = getDraggedObject(source);
    if (_scene == nullptr || (drag != nullptr && drag->_transformNode != nullptr)) {
        return;
    }
    auto hit = std::make_shared<VROHitTestResult>(hitTest(camera, origin, ray, true));
    _hitResultsBySource[source] = hit;
    // Mirror to the legacy single-source slot so subsystems that don't carry
    // a source ID (fuse, pinch, rotate) keep functioning. A passive source that
    // runs every frame (head gaze) opts out, or it would take that slot from
    // the pointer the user is actually aiming.
    if (mirrorToLegacy) {
        _hitResult = hit;
    }
}
std::shared_ptr<VROHitTestResult>
VROInputControllerBase::getHitResultForSource(int source) const {
    auto it = _hitResultsBySource.find(rayForSource(source));
    if (it != _hitResultsBySource.end() && it->second) {
        return it->second;
    }
    return _hitResult;
}

void VROInputControllerBase::onControllerStatus(int source, VROEventDelegate::ControllerStatus status){
    if (_currentControllerStatus == status){
        return;
    }

    _currentControllerStatus = status;

    std::shared_ptr<VRONode> focusedNode;
    if (_hitResult) {
        focusedNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnControllerStatus, _hitResult->getNode());
    }

    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onControllerStatus(source, status);
    }
    if (focusedNode != nullptr){
        focusedNode->getEventDelegate()->onControllerStatus(source, status);
    }
}

void VROInputControllerBase::onSwipe(int source, VROEventDelegate::SwipeState swipeState) {
    std::shared_ptr<VRONode> focusedNode;
    if (_hitResult) {
        focusedNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnSwipe, _hitResult->getNode());
    }

    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onSwipe(source, focusedNode, swipeState);
    }
    if (focusedNode != nullptr){
        focusedNode->getEventDelegate()->onSwipe(source, focusedNode, swipeState);
    }
}

void VROInputControllerBase::onScroll(int source, float x, float y) {
    std::shared_ptr<VRONode> focusedNode;
    if (_hitResult) {
        focusedNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnScroll, _hitResult->getNode());
    }

    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onScroll(source, focusedNode, x, y);
    }
    if (focusedNode != nullptr){
        focusedNode->getEventDelegate()->onScroll(source, focusedNode, x, y);
    }
}

void VROInputControllerBase::processGazeEvent(int source) {
    auto hit = getHitResultForSource(source);
    if (hit == nullptr) {
        return;
    }

    // Per-source hover state: only kicks in when the source-aware
    // updateHitNode(int source, ...) overload was used. Legacy single-pointer
    // backends still use the shared `_lastHoveredNode` path below.
    bool sourceAware = _hitResultsBySource.count(source) > 0;
    std::shared_ptr<VRONode> &lastHovered = sourceAware
        ? _lastHoveredNodesBySource[source]
        : _lastHoveredNode;
    HoverPending &pending = sourceAware
        ? _hoverPendingBySource[source]
        : _hoverPending;
    HoverExit &lastExit = sourceAware
        ? _hoverExitBySource[source]
        : _hoverExit;

    std::shared_ptr<VRONode> newNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnHover,
                                                                hit->getNode());
    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onGazeHit(source, newNode, *hit.get());
    }

    // Hysteresis: if the hit-test result returns to the currently-hovered
    // node, cancel any in-flight pending exit and emit nothing — the user
    // never actually saw a transition.
    if (lastHovered == newNode) {
        pending = HoverPending{};
        // Input-only canvas planes need coordinates within the artboard, even
        // when the ray remains on the same Viro collider. Suppress stationary
        // rays so a static Rive panel does not advance at headset refresh rate.
        if (newNode && newNode->getDragTransform() == VRODragTransform::None &&
            newNode->getEventDelegate() && !hit->isBackgroundHit()) {
            const VROVector3f position = hit->getLocation();
            auto previous = _canvasHoverPositions.find(source);
            const double now = VROTimeCurrentMillis();
            auto lastDispatch = _canvasHoverDispatchMillis.find(source);
            const bool moved = previous == _canvasHoverPositions.end() ||
                position.distance(previous->second) > kCanvasHoverMinDistance;
            const bool due = lastDispatch == _canvasHoverDispatchMillis.end() ||
                now - lastDispatch->second >= kCanvasHoverMinIntervalMillis;
            if (moved && due) {
                _canvasHoverPositions[source] = position;
                _canvasHoverDispatchMillis[source] = now;
                newNode->getEventDelegate()->onHover(source, newNode, true,
                                                     {position.x, position.y, position.z});
            }
        }
        return;
    }
    _canvasHoverPositions.erase(source);
    _canvasHoverDispatchMillis.erase(source);

    VROVector3f hitLoc = hit->getLocation();
    std::vector<float> pos = {hitLoc.x, hitLoc.y, hitLoc.z};
    bool isBgHit = hit->isBackgroundHit();
    if (isBgHit) {
        pos.clear();
    }

    // First-ever hover into a node (no prior hovered) — fire enter immediately.
    // No exit to defer, so no point holding it in pending.
    if (lastHovered == nullptr) {
        if (newNode && newNode->getEventDelegate()) {
            newNode->getEventDelegate()->onHover(source, newNode, true, pos);
        }
        lastHovered = newNode;
        pending = HoverPending{};
        return;
    }

    // From here on `lastHovered != nullptr` and `newNode != lastHovered`.
    // Record / update the pending candidate. We confirm the change only
    // after `kHoverHysteresisMillis` of the new candidate persisting,
    // which absorbs the 1–3 frame ray-cast jitter of unsteady aim.
    double now = VROTimeCurrentMillis();
    if (pending.candidateNode != newNode || pending.startedMillis < 0) {
        pending.candidateNode  = newNode;
        pending.candidatePos   = hitLoc;
        pending.candidateBgHit = isBgHit;
        pending.startedMillis  = now;
        return;
    }
    if (now - pending.startedMillis < kHoverHysteresisMillis) {
        // Same candidate as before, but window not elapsed — keep waiting.
        pending.candidatePos   = hitLoc;
        pending.candidateBgHit = isBgHit;
        return;
    }

    // Window elapsed and candidate held — confirm the transition.
    if (newNode && newNode->getEventDelegate()) {
        newNode->getEventDelegate()->onHover(source, newNode, true, pos);
    }
    if (lastHovered && lastHovered->getEventDelegate()) {
        lastHovered->getEventDelegate()->onHover(source, lastHovered, false, pos);
    }
    lastExit.node = lastHovered;
    lastExit.leftMillis = pending.startedMillis;
    lastHovered = newNode;
    pending = HoverPending{};
}

void VROInputControllerBase::processOnFuseEvent(int source, std::shared_ptr<VRONode> newNode) {
    std::shared_ptr<VRONode> focusedNode = getNodeToHandleEvent(VROEventDelegate::EventAction::OnFuse, newNode);
    if (_currentFusedNode != focusedNode){
        notifyOnFuseEvent(source, kOnFuseReset);
        _fuseTriggerAtMillis = kOnFuseReset;
        _haveNotifiedOnFuseTriggered = false;
        _currentFusedNode = focusedNode;
    }

    // Do nothing if no onFuse node is found
    if (!focusedNode || !_currentFusedNode->getEventDelegate()){
        return;
    }

    if (_fuseTriggerAtMillis == kOnFuseReset){
        _fuseTriggerAtMillis = VROTimeCurrentMillis() + _currentFusedNode->getEventDelegate()->getTimeToFuse();
    }

    // Compare the fuse time with the current time to get the timeToFuseRatio and notify delegates.
    // When the timeToFuseRatio counts down to 0, it is an indication that the node has been "onFused".
    if (!_haveNotifiedOnFuseTriggered){
        float delta = _fuseTriggerAtMillis - VROTimeCurrentMillis();
        float timeToFuseRatio = delta / _currentFusedNode->getEventDelegate()->getTimeToFuse();

        if (timeToFuseRatio <= 0.0f){
            timeToFuseRatio = 0.0f;
            _haveNotifiedOnFuseTriggered = true;
        }

        notifyOnFuseEvent(source, timeToFuseRatio);
    }
}

void VROInputControllerBase::notifyCameraTransform(const VROCamera &camera) {
    if (_scene) {
        std::shared_ptr<VROEventDelegate> delegate = _scene->getRootNode()->getEventDelegate();

        if (delegate && delegate->isEventEnabled(VROEventDelegate::EventAction::OnCameraTransformUpdate)) {
            delegate->onCameraTransformUpdate(camera.getPosition(), camera.getRotation().toEuler(),
                                              camera.getForward(), camera.getUp());
        }
    }
}

void VROInputControllerBase::notifyOnFuseEvent(int source, float timeToFuseRatio) {
    for (std::shared_ptr<VROEventDelegate> delegate : _delegates) {
        delegate->onFuse(source, _currentFusedNode, timeToFuseRatio);
    }

    if (_currentFusedNode && _currentFusedNode->getEventDelegate()){
        _currentFusedNode->getEventDelegate()->onFuse(source, _currentFusedNode, timeToFuseRatio);
    }
}

VROHitTestResult VROInputControllerBase::hitTest(const VROCamera &camera, VROVector3f origin, VROVector3f ray, bool boundsOnly) {
    std::vector<VROHitTestResult> results;
    std::shared_ptr<VROPortal> sceneRootNode = _scene->getRootNode();

    // Grab all the nodes that were hit
    std::vector<VROHitTestResult> nodeResults = sceneRootNode->hitTest(camera, origin, ray, boundsOnly);
    results.insert(results.end(), nodeResults.begin(), nodeResults.end());

    // Sort and get the closest node
    std::sort(results.begin(), results.end(), [](VROHitTestResult a, VROHitTestResult b) {
        return a.getDistance() < b.getDistance();
    });

    // Return the closest hit element, if any.
    for (int i = 0; i < results.size(); i++) {
        if (!results[i].getNode()->getIgnoreEventHandling()) {
            return results[i];
        }
    }
    
    VROVector3f backgroundPosition = origin + (ray * kSceneBackgroundDistance);
    VROHitTestResult sceneBackgroundHitResult = { sceneRootNode, backgroundPosition,
                                                  kSceneBackgroundDistance, true, camera };
    return sceneBackgroundHitResult;
}

std::shared_ptr<VRONode> VROInputControllerBase::getNodeToHandleEvent(VROEventDelegate::EventAction action,
                                                                      std::shared_ptr<VRONode> node){
    // Base condition, we are asking for the scene's root node's parent, return.
    if (node == nullptr) {
        return nullptr;
    }

    std::shared_ptr<VROEventDelegate> delegate = node->getEventDelegate();
    if (delegate != nullptr && delegate->isEventEnabled(action)){
        return node;
    } else {
        return getNodeToHandleEvent(action, node->getParentNode());
    }
}
