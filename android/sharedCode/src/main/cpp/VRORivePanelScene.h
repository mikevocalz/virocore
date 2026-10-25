//
//  VRORivePanelScene.h
//  ViroRenderer
//
//  The rive-runtime half of VRORivePanel: render context, File, artboard,
//  state machine and view-model instance. Lives on the panel's worker thread
//  and requires that thread's EGL context to be current for every call,
//  including destruction. No rive type appears in this header; the .cpp is
//  the only VRORivePanel source that includes rive headers and is compiled
//  through viro_rive_sources() (C++17, no RTTI or exceptions, hidden).
//

#pragma once

#include "VRORivePanelGeometry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class __attribute__((visibility("hidden"))) VRORivePanelScene {
public:
    // Creates rive::gpu::RenderContextGLImpl on the current context.
    static std::unique_ptr<VRORivePanelScene> create(std::string *error);
    ~VRORivePanelScene();
    VRORivePanelScene(const VRORivePanelScene &) = delete;
    VRORivePanelScene &operator=(const VRORivePanelScene &) = delete;

    // Imports the file and instantiates the named artboard and state machine
    // (empty = default). On failure the previous content is kept.
    bool load(const uint8_t *bytes, size_t length, const std::string &artboard,
              const std::string &stateMachine, std::string *error);
    // Re-instantiates on the current file. A state machine that the new
    // artboard lacks falls back to the default and reports it in *error
    // while still returning true.
    bool selectArtboard(const std::string &artboard, const std::string &stateMachine,
                        std::string *error);
    bool selectStateMachine(const std::string &stateMachine, std::string *error);

    bool hasFile() const;
    bool hasArtboard() const;
    VRORiveRect artboardBounds() const;

    bool setNumber(const std::string &path, float value, std::string *error);
    bool setBoolean(const std::string &path, bool value, std::string *error);
    bool setString(const std::string &path, const std::string &value, std::string *error);
    bool fireTrigger(const std::string &path, std::string *error);

    // Artboard-space position, already routed (see VRORivePointerRouter).
    void pointer(VRORivePointerAction action, float artboardX, float artboardY, int pointerId);

    // Advances the state machine (or the artboard when the file has none).
    // Returns true while more frames are needed.
    bool advance(float elapsedSeconds);

    // Draws one frame into the current window surface's framebuffer 0.
    bool render(int widthPx, int heightPx, const VRORiveAffine &artboardToPanel,
                float cornerRadiusPx, uint32_t backgroundColor, std::string *error);

    // Drops the framebuffer render target (the window surface it described is
    // going away). Rive resources stay.
    void releaseRenderTarget();

    struct State;

private:
    explicit VRORivePanelScene(std::unique_ptr<State> state);
    std::unique_ptr<State> _state;
};
