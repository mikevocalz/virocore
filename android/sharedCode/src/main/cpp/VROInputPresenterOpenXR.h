// VROInputPresenterOpenXR.h
// ViroRenderer
//
// Visual presenter for the OpenXR (Meta Quest) input controller. Provides:
//   1. A reticle (existing VROReticle) positioned at the hit point in world
//      space (not headlocked — Cardboard-style fixed pointer is wrong here
//      because we have actual 6DOF aim from controllers / FB hand-aim).
//   2. Two laser lines, one per hand, drawn from the aim origin to the hit
//      (or a fixed forward distance when nothing is hit). Each laser is
//      independent so apps can show both controllers / both hands at once.
//
// All laser nodes are children of the presenter's _rootNode, which VROScene
// attaches to the scene root via attachInputController(). The reticle position
// is updated via the existing onGazeHit → onReticleGazeHit chain. The laser
// endpoints are updated each frame by VROInputControllerOpenXR via
// updateAimRay(source, ...).
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROINPUTPRESENTEROPENXR_H
#define ANDROID_VROINPUTPRESENTEROPENXR_H

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <unistd.h>
#include "VROInputPresenter.h"
#include "VROReticle.h"
#include "VRONode.h"
#include "VROPolyline.h"
#include "VROSphere.h"
#include "VROMaterial.h"
#include "VROGLTFLoader.h"
#include "VROPlatformUtil.h"
#include "VROInputType.h"
#include "VROLog.h"
#include "VROOpenXRPlatform.h"

class VROInputPresenterOpenXR : public VROInputPresenter {
public:
    VROInputPresenterOpenXR() {
        // Reticle at world-space hit point (not headlocked).
        auto reticle = std::make_shared<VROReticle>(nullptr);
        reticle->setPointerFixed(false);
        setReticle(reticle);
    }

    ~VROInputPresenterOpenXR() override = default;

    /**
     * Inherit the existing reticle-positioning chain. processGazeEvent() in
     * VROInputControllerBase calls onGazeHit() on registered delegates; we
     * forward to onReticleGazeHit() which moves the reticle to hit world pos.
     */
    void onGazeHit(int source, std::shared_ptr<VRONode> node, const VROHitTestResult &hit) override {
        VROInputPresenter::onReticleGazeHit(hit);
    }

    /**
     * Trigger reticle pulse animation on click for parity with OVR/Cardboard.
     */
    void onClick(int source, std::shared_ptr<VRONode> node, ClickState clickState,
                 std::vector<float> position) override {
        VROInputPresenter::onClick(source, node, clickState, position);
        if (clickState == ClickState::ClickUp && getReticle()) {
            getReticle()->trigger();
        }
    }

    /**
     * Update one source's aim ray each frame. Each source (right pointer,
     * left pointer) keeps its own polyline so both can be visible at once.
     *
     * Mutates the persistent polyline's points in place via setPaths(). This
     * avoids the one-frame blink that occurs when VRONode::setGeometry()
     * swaps the geometry pointer and the new mesh hasn't been uploaded to
     * the GPU yet.
     *
     * @param source     Source ID (e.g. ViroOculus::Controller, LeftController)
     * @param origin     World-space origin of the ray
     * @param hitPoint   World-space endpoint
     * @param visible    True to show, false to hide (no input active)
     */
    void updateAimRay(int source, const VROVector3f &origin,
                      const VROVector3f &hitPoint, bool visible, bool surfaceHit = false) {
        Laser &laser = getOrCreateLaser(source);
        if (!visible) {
            laser.node->setHidden(true);
            laser.dot->setHidden(true);
            return;
        }
        // Defensive: a zero-length segment (origin == hitPoint, e.g. degenerate
        // forward direction or a hit reported exactly at the controller pose)
        // produces an invisible polyline. Treat as "no aim available".
        VROVector3f delta = hitPoint - origin;
        if (delta.magnitude() < 0.001f) {
            laser.node->setHidden(true);
            laser.dot->setHidden(true);
            return;
        }
        laser.node->setHidden(false);
        std::vector<std::vector<VROVector3f>> paths = { { origin, hitPoint } };
        laser.geom->setPaths(paths);
        // Each ray owns its endpoint. The shared legacy reticle can follow the
        // other hand, so it cannot be the only indication of this ray's hit.
        laser.dot->setHidden(!surfaceHit || !getReticle() || !getReticle()->isEnabled());
        laser.dot->setPosition(hitPoint);

    }

    /*
     * Create one hidden node per controller source, parented to the presenter
     * root. No geometry yet: VROInputControllerOpenXR decides per hand whether
     * the runtime supplies a render model (XR_FB_render_model on Meta Quest) or
     * the fallback GLB is used, then calls loadControllerMeshFile().
     */
    void createControllerMeshNodes(std::shared_ptr<VRODriver> driver) {
        if (!driver || !_meshNodes.empty()) return;
        _driver = driver;
        const int sources[] = { ViroOculus::Controller, ViroOculus::LeftController };
        for (int source : sources) {
            auto node = std::make_shared<VRONode>();
            node->setName("ControllerMesh");
            node->setHidden(true);                 // shown once a grip pose arrives
            node->setSelectable(false);            // never a hit-test target (own controller)
            node->setIgnoreEventHandling(true);
            _rootNode->addChildNode(node);
            _meshNodes[source] = node;
        }
    }

    /*
     * GLB used when the runtime has no render model for this controller. Prefers
     * the model the PICO runtime itself renders (system image, world-readable,
     * one file per hand), else the bundled neutral GLB.
     *
     * ponytail: hardcoded PICO 4 Ultra ("sparrow") path. The portable upgrade is
     * XR_EXT_render_model / XR_EXT_interaction_render_model, which the PICO
     * runtime implements.
     */
    static std::string fallbackControllerGlbPath(int source) {
        const char *real = (source == ViroOculus::LeftController)
            ? "/system/media/PvrRes/controller/PICO4U/o_com_sparrow_left_01.glb"
            : "/system/media/PvrRes/controller/PICO4U/o_com_sparrow_right_01.glb";
        if (access(real, R_OK) == 0) {
            return std::string(real);
        }
        return VROOpenXRBundledAssetPath("controller_neutral.glb");
    }

    /*
     * Load a binary glTF from a local file into `source`'s controller node. Each
     * load goes into its own child container that replaces the previous one, so
     * a superseded load that finishes late lands in a detached node and never
     * doubles up the model. The model's origin is expected at the grip pose.
     */
    void loadControllerMeshFile(int source, const std::string &glbPath,
                                std::function<void(bool)> onLoaded) {
        auto it = _meshNodes.find(source);
        std::shared_ptr<VRODriver> driver = _driver.lock();
        if (it == _meshNodes.end() || !driver || glbPath.empty()) return;

        for (const auto &child : it->second->getChildNodes()) {
            child->removeFromParentNode();
        }
        auto container = std::make_shared<VRONode>();
        container->setSelectable(false);
        container->setIgnoreEventHandling(true);
        it->second->addChildNode(container);

        VROGLTFLoader::loadGLTFFromResource(
            glbPath, {}, VROResourceType::LocalFile, container, /*isGLTFBinary=*/true, driver,
            [source, glbPath, onLoaded](std::shared_ptr<VRONode> n, bool success) {
                if (!success) {
                    pwarn("[VROInputOpenXR] controller mesh load failed for source %d (%s)",
                          source, glbPath.c_str());
                }
                if (onLoaded) {
                    // Parse failures report from a background thread; state
                    // belongs to the render thread.
                    VROPlatformDispatchAsyncRenderer([onLoaded, success] { onLoaded(success); });
                }
            });
    }

    /**
     * Position the controller mesh for `source` at its grip pose, or hide it when
     * `visible` is false (controller inactive / not located). No-op before the
     * node exists.
     */
    void updateControllerMesh(int source, const VROVector3f &pos,
                              const VROQuaternion &rot, bool visible) {
        auto it = _meshNodes.find(source);
        if (it == _meshNodes.end()) return;
        std::shared_ptr<VRONode> &node = it->second;
        if (!visible) {
            node->setHidden(true);
            return;
        }
        node->setHidden(false);
        node->setPosition(pos);
        node->setRotation(rot);
    }

private:
    struct Laser {
        std::shared_ptr<VRONode>     node;
        std::shared_ptr<VROPolyline> geom;
        std::shared_ptr<VRONode>     dot;
    };

    Laser &getOrCreateLaser(int source) {
        auto it = _lasers.find(source);
        if (it != _lasers.end()) {
            return it->second;
        }

        Laser laser;
        std::vector<VROVector3f> initialPath = { {0, 0, 0}, {0, 0, -1} };
        laser.geom = VROPolyline::createPolyline(initialPath, 0.006f /* thickness */);
        laser.geom->setName("AimLaserGeom");
        auto material = laser.geom->getMaterials().front();
        material->getDiffuse().setColor({ 0.33f, 0.976f, 0.968f, 1.0f }); // cyan
        material->setWritesToDepthBuffer(false);
        material->setReadsFromDepthBuffer(false);
        material->setReceivesShadows(false);

        laser.node = std::make_shared<VRONode>();
        laser.node->setName("AimLaser");
        laser.node->setRenderingOrder(1000);
        laser.node->setGeometry(laser.geom);
        laser.node->setHidden(true);  // hidden until first updateAimRay()
        // The beam's AABB spans controller to hit point, so its own ray runs
        // through it corner to corner and would out-sort the aimed node.
        laser.node->setSelectable(false);
        laser.node->setIgnoreEventHandling(true);
        _rootNode->addChildNode(laser.node);

        // A small unlit dot remains readable against the canvas. Draw it after
        // scene geometry, without depth writes, and never include it in hits.
        auto dotGeometry = VROSphere::createSphere(0.007f, 12, 8, true);
        auto dotMaterial = dotGeometry->getMaterials().front();
        dotMaterial->setLightingModel(VROLightingModel::Constant);
        dotMaterial->getDiffuse().setColor({0.33f, 0.976f, 0.968f, 1.0f});
        dotMaterial->setReadsFromDepthBuffer(false);
        dotMaterial->setWritesToDepthBuffer(false);
        dotMaterial->setReceivesShadows(false);
        laser.dot = std::make_shared<VRONode>();
        laser.dot->setName("AimHitDot");
        laser.dot->setGeometry(dotGeometry);
        laser.dot->setRenderingOrder(1001);
        laser.dot->setHidden(true);
        laser.dot->setSelectable(false);
        laser.dot->setIgnoreEventHandling(true);
        _rootNode->addChildNode(laser.dot);

        return _lasers.emplace(source, std::move(laser)).first->second;
    }

    std::unordered_map<int, Laser> _lasers;

    // Per-hand controller mesh nodes (source id → node). Created by
    // createControllerMeshNodes(), filled by loadControllerMeshFile(), positioned each frame by updateControllerMesh().
    std::unordered_map<int, std::shared_ptr<VRONode>> _meshNodes;
    std::weak_ptr<VRODriver> _driver;
};

#endif // ANDROID_VROINPUTPRESENTEROPENXR_H
