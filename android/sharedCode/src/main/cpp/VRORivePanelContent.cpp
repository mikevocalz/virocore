//
//  VRORivePanelContent.cpp
//  ViroRenderer
//
//  Compiled through viro_rive_sources() with the flags the rive archives use.
//

#include "VRORivePanelContent.h"

#include "rive/viewmodel/runtime/viewmodel_runtime.hpp"

#include <utility>

VRORivePanelContent &VRORivePanelContent::operator=(VRORivePanelContent &&other) noexcept {
    if (this == &other) return *this;
    reset();
    artboard = std::move(other.artboard);
    stateMachine = std::move(other.stateMachine);
    viewModel = std::move(other.viewModel);
    return *this;
}

void VRORivePanelContent::reset() {
    viewModel = nullptr;
    stateMachine.reset();
    artboard.reset();
}

std::unique_ptr<rive::StateMachineInstance> VRORivePanelMakeStateMachine(
    rive::ArtboardInstance *artboard, const std::string &name) {
    if (!name.empty()) {
        return artboard->stateMachineNamed(name);
    }
    // defaultStateMachine() is null unless the designer marked one.
    std::unique_ptr<rive::StateMachineInstance> machine = artboard->defaultStateMachine();
    if (machine == nullptr && artboard->stateMachineCount() > 0) {
        machine = artboard->stateMachineAt(0);
    }
    return machine;
}

namespace {

// Binds the artboard's default view-model instance. Files without a view model
// linked to the artboard get none, and view-model writes report that.
void bindViewModel(const rive::File &file, VRORivePanelContent &content) {
    // File owns the ViewModelRuntime it returns (file.cpp:2343-2366).
    rive::ViewModelRuntime *viewModel = file.defaultArtboardViewModel(content.artboard.get());
    if (viewModel == nullptr) return;
    content.viewModel = viewModel->createDefaultInstance();
    if (content.viewModel == nullptr) return;
    if (content.stateMachine != nullptr) {
        content.stateMachine->bindViewModelInstance(content.viewModel->instance());
    } else {
        content.artboard->bindViewModelInstance(content.viewModel->instance());
    }
}

} // namespace

bool VRORivePanelInstantiate(const rive::File &file, const std::string &artboardName,
                             const std::string &stateMachineName, VRORivePanelContent *out,
                             std::string *error) {
    VRORivePanelContent content;
    content.artboard = artboardName.empty() ? file.artboardDefault()
                                            : file.artboardNamed(artboardName);
    if (content.artboard == nullptr) {
        *error = artboardName.empty() ? "file has no artboard"
                                      : "no artboard named \"" + artboardName + "\"";
        return false;
    }
    content.stateMachine = VRORivePanelMakeStateMachine(content.artboard.get(), stateMachineName);
    if (content.stateMachine == nullptr && !stateMachineName.empty()) {
        *error = "artboard \"" + content.artboard->name() + "\" has no state machine named \"" +
                 stateMachineName + "\"; using its default";
        content.stateMachine = VRORivePanelMakeStateMachine(content.artboard.get(), std::string());
    }
    bindViewModel(file, content);
    // Apply the initial state so the first frame is not the setup pose.
    if (content.stateMachine != nullptr) {
        content.stateMachine->advanceAndApply(0.0f);
    } else {
        content.artboard->advance(0.0f);
    }
    *out = std::move(content);
    return true;
}
