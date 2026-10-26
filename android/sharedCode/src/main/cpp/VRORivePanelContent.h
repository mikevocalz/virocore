//
//  VRORivePanelContent.h
//  ViroRenderer
//
//  The instantiated half of a Rive file for VRORivePanelScene: artboard
//  instance, state machine instance and view-model instance. Includes rive
//  headers, so only rive-flagged sources (viro_rive_sources) and the host test
//  include it.
//
//  Teardown order is load bearing. A state machine's animation instances
//  remove the data binds they parked on the artboard when they are destroyed
//  (LinearAnimationInstance::~LinearAnimationInstance), so the state machine
//  must go before its artboard, and the view model before the state machine
//  it is bound to. reset() does that, and every path that drops content goes
//  through it: the destructor and move assignment. An implicit move
//  assignment would release the old artboard first (declaration order) and
//  the old state machine would then read freed memory.
//

#pragma once

#include "rive/animation/state_machine_instance.hpp"
#include "rive/artboard.hpp"
#include "rive/file.hpp"
#include "rive/viewmodel/runtime/viewmodel_instance_runtime.hpp"

#include <memory>
#include <string>

class __attribute__((visibility("hidden"))) VRORivePanelContent {
public:
    VRORivePanelContent() = default;
    ~VRORivePanelContent() { reset(); }
    // Constructing from another leaves nothing of ours to tear down.
    VRORivePanelContent(VRORivePanelContent &&other) noexcept = default;
    // Tears this content down with reset() before taking other's.
    VRORivePanelContent &operator=(VRORivePanelContent &&other) noexcept;
    VRORivePanelContent(const VRORivePanelContent &) = delete;
    VRORivePanelContent &operator=(const VRORivePanelContent &) = delete;

    // View model, then state machine, then artboard.
    void reset();

    std::unique_ptr<rive::ArtboardInstance> artboard;
    std::unique_ptr<rive::StateMachineInstance> stateMachine;
    rive::rcp<rive::ViewModelInstanceRuntime> viewModel;
};

// The named state machine, or the default one (the designer's, else the
// first) when name is empty. Null when there is none.
std::unique_ptr<rive::StateMachineInstance> VRORivePanelMakeStateMachine(
    rive::ArtboardInstance *artboard, const std::string &name)
    __attribute__((visibility("hidden")));

// Instantiates artboardName (empty: default) and stateMachineName on file,
// binds the artboard's default view-model instance and applies the initial
// state. False with *error set when the artboard does not exist; *out is left
// untouched then. A missing state machine falls back to the default and is
// reported in *error while still returning true.
bool VRORivePanelInstantiate(const rive::File &file, const std::string &artboardName,
                             const std::string &stateMachineName, VRORivePanelContent *out,
                             std::string *error) __attribute__((visibility("hidden")));
