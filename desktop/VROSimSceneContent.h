//
//  VROSimSceneContent.h
//
//  Test content for viro_sim_host: a procedural checker texture and a light
//  rig, so captures exercise Viro's Metal texture sampling and lighting
//  paths instead of a single unlit color.
//
//  SOT-KEYWORDS: test-scene, texture, lighting, simulator
//

#ifndef VROSimSceneContent_h
#define VROSimSceneContent_h

#include <memory>

class VROTexture;
class VRONode;

// RGBA8 sRGB checker, `cells` squares per side, alternating orange and white.
std::shared_ptr<VROTexture> VROSimMakeCheckerTexture(int size, int cells);

// One ambient and one directional light, so each cube face shades differently.
void VROSimAddLights(const std::shared_ptr<VRONode> &root);

#endif /* VROSimSceneContent_h */
