//
//  VRORivePanelScene.cpp
//  ViroRenderer
//
//  Compiled through viro_rive_sources() with the flags the rive archives use.
//  rive-runtime pin: android/rive/RIVE_RUNTIME_COMMIT (8b3ca41b).
//
//  Render target: FramebufferRenderTargetGL on the window surface's
//  framebuffer 0 (render_target_gl.hpp:191). This is the path rive-android
//  takes on its own SurfaceView/TextureView surfaces. When the device has
//  EXT_shader_pixel_local_storage Rive draws straight into FBO 0; otherwise it
//  renders into an internal texture and blits back at the end of the pass
//  (allocateOffscreenTargetTexture, render_target_gl.cpp:327), which is the
//  same cost as doing that blit ourselves.
//  Alternative, not taken: a TextureRenderTargetGL on a texture we own plus
//  one blit/draw to FBO 0. It always pays the blit but would let the same
//  texture feed the B2 compositor ring. Pick by measuring both on Quest 3 and
//  PICO 4 Ultra (Rive frame time and GPU time per frame).
//
//  GL state: this context belongs to the panel worker alone; nothing else
//  issues GL on it, so RenderContextGLImpl::invalidateGLState() and
//  unbindGLInternalResources() (render_context_gl_impl.hpp:79,88) are not
//  needed between frames. If anything ever draws on this context besides
//  Rive, call unbindGLInternalResources() before it and invalidateGLState()
//  after it.
//

#include "VRORivePanelScene.h"

#include "rive/animation/state_machine_instance.hpp"
#include "rive/artboard.hpp"
#include "rive/file.hpp"
#include "rive/math/circle_constant.hpp"
#include "rive/math/raw_path.hpp"
#include "rive/renderer/gl/render_context_gl_impl.hpp"
#include "rive/renderer/gl/render_target_gl.hpp"
#include "rive/renderer/render_context.hpp"
#include "rive/renderer/rive_renderer.hpp"
#include "rive/viewmodel/runtime/viewmodel_instance_runtime.hpp"
#include "rive/viewmodel/runtime/viewmodel_runtime.hpp"

#include <android/log.h>

#define RIVE_PANEL_LOG_TAG "ViroRivePanel"

namespace {

struct Content {
    // Destroyed bottom-up: view model, state machine, then the artboard the
    // state machine points into.
    std::unique_ptr<rive::ArtboardInstance> artboard;
    std::unique_ptr<rive::StateMachineInstance> stateMachine;
    rive::rcp<rive::ViewModelInstanceRuntime> viewModel;
};

const char *importResultName(rive::ImportResult result) {
    switch (result) {
        case rive::ImportResult::success: return "success";
        case rive::ImportResult::unsupportedVersion: return "unsupported version";
        case rive::ImportResult::malformed: return "malformed";
    }
    return "unknown";
}

std::unique_ptr<rive::StateMachineInstance> makeStateMachine(rive::ArtboardInstance *artboard,
                                                             const std::string &name) {
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

// Binds the artboard's default view-model instance. Files without a view model
// linked to the artboard get none, and view-model writes report that.
void bindViewModel(const rive::File &file, Content &content) {
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

bool instantiate(const rive::File &file, const std::string &artboardName,
                 const std::string &stateMachineName, Content *out, std::string *error) {
    Content content;
    content.artboard = artboardName.empty() ? file.artboardDefault()
                                            : file.artboardNamed(artboardName);
    if (content.artboard == nullptr) {
        *error = artboardName.empty() ? "file has no artboard"
                                      : "no artboard named \"" + artboardName + "\"";
        return false;
    }
    content.stateMachine = makeStateMachine(content.artboard.get(), stateMachineName);
    if (content.stateMachine == nullptr && !stateMachineName.empty()) {
        *error = "artboard \"" + content.artboard->name() + "\" has no state machine named \"" +
                 stateMachineName + "\"; using its default";
        content.stateMachine = makeStateMachine(content.artboard.get(), std::string());
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

// The panel's shape in panel pixels: a rect, or a rounded rect for r > 0.
rive::rcp<rive::RenderPath> makePanelShapePath(rive::Factory *factory, float w, float h, float r) {
    rive::RawPath path;
    if (r <= 0.0f) {
        path.addRect(rive::AABB(0.0f, 0.0f, w, h));
        return factory->makeRenderPath(path, rive::FillRule::nonZero);
    }
    // Cubic quarter circles, same constant rive uses for ovals.
    const float c = r * rive::icircleConstant;
    path.moveTo(r, 0.0f);
    path.lineTo(w - r, 0.0f);
    path.cubicTo(w - c, 0.0f, w, c, w, r);
    path.lineTo(w, h - r);
    path.cubicTo(w, h - c, w - c, h, w - r, h);
    path.lineTo(r, h);
    path.cubicTo(c, h, 0.0f, h - c, 0.0f, h - r);
    path.lineTo(0.0f, r);
    path.cubicTo(0.0f, c, c, 0.0f, r, 0.0f);
    path.close();
    return factory->makeRenderPath(path, rive::FillRule::nonZero);
}

} // namespace

struct VRORivePanelScene::State {
    // Declared first, destroyed last: every rive object below may hold GPU
    // resources from this context.
    std::unique_ptr<rive::gpu::RenderContext> context;
    rive::rcp<rive::gpu::FramebufferRenderTargetGL> target;
    int targetWidth = 0;
    int targetHeight = 0;

    rive::rcp<rive::File> file;
    Content content;

    rive::rcp<rive::RenderPath> shapePath;
    float shapeWidth = 0.0f, shapeHeight = 0.0f, shapeRadius = -1.0f;
    rive::rcp<rive::RenderPaint> backgroundPaint;

    uint64_t frameNumber = 0;
};

std::unique_ptr<VRORivePanelScene> VRORivePanelScene::create(std::string *error) {
    std::unique_ptr<State> state(new State());
    state->context = rive::gpu::RenderContextGLImpl::MakeContext();
    if (state->context == nullptr) {
        *error = "RenderContextGLImpl::MakeContext failed (GLES 3.0 or required "
                 "extensions missing)";
        return nullptr;
    }
    return std::unique_ptr<VRORivePanelScene>(new VRORivePanelScene(std::move(state)));
}

VRORivePanelScene::VRORivePanelScene(std::unique_ptr<State> state) : _state(std::move(state)) {}

VRORivePanelScene::~VRORivePanelScene() = default;

bool VRORivePanelScene::load(const uint8_t *bytes, size_t length, const std::string &artboard,
                             const std::string &stateMachine, std::string *error) {
    rive::ImportResult result = rive::ImportResult::malformed;
    // The render context is the factory, so paths, paints and fonts are
    // created as GPU-backed objects of this context.
    rive::rcp<rive::File> file =
        rive::File::import(rive::Span<const uint8_t>(bytes, length), _state->context.get(), &result);
    if (file == nullptr || result != rive::ImportResult::success) {
        *error = std::string("rive::File::import: ") + importResultName(result);
        return false;
    }
    Content content;
    std::string instantiateNote;
    if (!instantiate(*file, artboard, stateMachine, &content, &instantiateNote)) {
        *error = instantiateNote;
        return false;
    }
    _state->content = Content();
    _state->file = std::move(file);
    _state->content = std::move(content);
    *error = instantiateNote;
    return true;
}

bool VRORivePanelScene::selectArtboard(const std::string &artboard, const std::string &stateMachine,
                                       std::string *error) {
    if (_state->file == nullptr) {
        return true; // Applied when a file loads.
    }
    Content content;
    if (!instantiate(*_state->file, artboard, stateMachine, &content, error)) {
        return false;
    }
    _state->content = std::move(content);
    return true;
}

bool VRORivePanelScene::selectStateMachine(const std::string &stateMachine, std::string *error) {
    if (_state->content.artboard == nullptr) {
        return true;
    }
    std::unique_ptr<rive::StateMachineInstance> machine =
        makeStateMachine(_state->content.artboard.get(), stateMachine);
    if (machine == nullptr) {
        *error = stateMachine.empty()
                     ? "artboard \"" + _state->content.artboard->name() + "\" has no state machine"
                     : "artboard \"" + _state->content.artboard->name() +
                           "\" has no state machine named \"" + stateMachine + "\"";
        return false;
    }
    _state->content.stateMachine = std::move(machine);
    if (_state->content.viewModel != nullptr) {
        _state->content.stateMachine->bindViewModelInstance(_state->content.viewModel->instance());
    }
    _state->content.stateMachine->advanceAndApply(0.0f);
    return true;
}

bool VRORivePanelScene::hasFile() const { return _state->file != nullptr; }

bool VRORivePanelScene::hasArtboard() const { return _state->content.artboard != nullptr; }

VRORiveRect VRORivePanelScene::artboardBounds() const {
    if (_state->content.artboard == nullptr) return {};
    const rive::AABB bounds = _state->content.artboard->bounds();
    return VRORiveRect{bounds.left(), bounds.top(), bounds.right(), bounds.bottom()};
}

namespace {

bool requireViewModel(const Content &content, const std::string &path, std::string *error) {
    if (content.viewModel != nullptr) return true;
    *error = content.artboard == nullptr
                 ? "view-model write to \"" + path + "\" before any artboard is loaded"
                 : "artboard \"" + content.artboard->name() + "\" has no view model; dropped \"" +
                       path + "\"";
    return false;
}

std::string missingProperty(const char *kind, const std::string &path) {
    return std::string("view model has no ") + kind + " property \"" + path + "\"";
}

} // namespace

bool VRORivePanelScene::setNumber(const std::string &path, float value, std::string *error) {
    if (!requireViewModel(_state->content, path, error)) return false;
    rive::ViewModelInstanceNumberRuntime *property = _state->content.viewModel->propertyNumber(path);
    if (property == nullptr) {
        *error = missingProperty("number", path);
        return false;
    }
    property->value(value);
    return true;
}

bool VRORivePanelScene::setBoolean(const std::string &path, bool value, std::string *error) {
    if (!requireViewModel(_state->content, path, error)) return false;
    rive::ViewModelInstanceBooleanRuntime *property =
        _state->content.viewModel->propertyBoolean(path);
    if (property == nullptr) {
        *error = missingProperty("boolean", path);
        return false;
    }
    property->value(value);
    return true;
}

bool VRORivePanelScene::setString(const std::string &path, const std::string &value,
                                  std::string *error) {
    if (!requireViewModel(_state->content, path, error)) return false;
    rive::ViewModelInstanceStringRuntime *property = _state->content.viewModel->propertyString(path);
    if (property == nullptr) {
        *error = missingProperty("string", path);
        return false;
    }
    property->value(value);
    return true;
}

bool VRORivePanelScene::fireTrigger(const std::string &path, std::string *error) {
    if (!requireViewModel(_state->content, path, error)) return false;
    rive::ViewModelInstanceTriggerRuntime *property =
        _state->content.viewModel->propertyTrigger(path);
    if (property == nullptr) {
        *error = missingProperty("trigger", path);
        return false;
    }
    property->trigger();
    return true;
}

void VRORivePanelScene::pointer(VRORivePointerAction action, float artboardX, float artboardY,
                                int pointerId) {
    rive::StateMachineInstance *machine = _state->content.stateMachine.get();
    if (machine == nullptr) return;
    const rive::Vec2D position(artboardX, artboardY);
    // state_machine_instance.hpp:193-203. pointerDown/Up take no timestamp at
    // this pin; pointerMove's defaults to 0.
    switch (action) {
        case VRORivePointerAction::Down: machine->pointerDown(position, pointerId); break;
        case VRORivePointerAction::Move: machine->pointerMove(position, 0.0f, pointerId); break;
        case VRORivePointerAction::Up: machine->pointerUp(position, pointerId); break;
        case VRORivePointerAction::Exit: machine->pointerExit(position, pointerId); break;
        case VRORivePointerAction::Drop: break;
    }
}

bool VRORivePanelScene::advance(float elapsedSeconds) {
    if (_state->content.stateMachine != nullptr) {
        return _state->content.stateMachine->advanceAndApply(elapsedSeconds);
    }
    if (_state->content.artboard != nullptr) {
        return _state->content.artboard->advance(elapsedSeconds);
    }
    return false;
}

void VRORivePanelScene::releaseRenderTarget() {
    _state->target = nullptr;
    _state->targetWidth = 0;
    _state->targetHeight = 0;
}

bool VRORivePanelScene::render(int widthPx, int heightPx, const VRORiveAffine &artboardToPanel,
                               float cornerRadiusPx, uint32_t backgroundColor, std::string *error) {
    State &s = *_state;
    if (s.target == nullptr || s.targetWidth != widthPx || s.targetHeight != heightPx) {
        GLint samples = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glGetIntegerv(GL_SAMPLES, &samples);
        s.target = rive::make_rcp<rive::gpu::FramebufferRenderTargetGL>(
            uint32_t(widthPx), uint32_t(heightPx), 0u, uint32_t(samples > 0 ? samples : 0));
        s.targetWidth = widthPx;
        s.targetHeight = heightPx;
    }

    const float w = float(widthPx);
    const float h = float(heightPx);
    const float r = VRORiveClampCornerRadius(cornerRadiusPx, w, h);
    if (s.shapePath == nullptr || s.shapeWidth != w || s.shapeHeight != h || s.shapeRadius != r) {
        s.shapePath = makePanelShapePath(s.context.get(), w, h, r);
        s.shapeWidth = w;
        s.shapeHeight = h;
        s.shapeRadius = r;
    }

    rive::gpu::RenderContext::FrameDescriptor frame;
    frame.renderTargetWidth = uint32_t(widthPx);
    frame.renderTargetHeight = uint32_t(heightPx);
    frame.loadAction = rive::gpu::LoadAction::clear;
    frame.clearColor = 0x00000000u; // Transparent; corners stay transparent.
    s.context->beginFrame(frame);
    {
        rive::RiveRenderer renderer(s.context.get());
        renderer.save();
        if (r > 0.0f) {
            // Anti-aliased in Rive's coverage-based clip modes; in MSAA mode
            // the clip is antialiased by the samples.
            renderer.clipPath(s.shapePath.get());
        }
        if ((backgroundColor >> 24) != 0) {
            if (s.backgroundPaint == nullptr) {
                s.backgroundPaint = s.context->makeRenderPaint();
                s.backgroundPaint->style(rive::RenderPaintStyle::fill);
            }
            s.backgroundPaint->color(backgroundColor);
            renderer.drawPath(s.shapePath.get(), s.backgroundPaint.get());
        }
        if (s.content.artboard != nullptr) {
            renderer.transform(rive::Mat2D(artboardToPanel.xx, artboardToPanel.xy,
                                           artboardToPanel.yx, artboardToPanel.yy,
                                           artboardToPanel.tx, artboardToPanel.ty));
            s.content.artboard->draw(&renderer);
        }
        renderer.restore();
    }
    rive::gpu::RenderContext::FlushResources flush;
    flush.renderTarget = s.target.get();
    // GL syncs buffer reuse itself; frame numbers only drive Rive's resource
    // purgatory (gpu_resource.cpp).
    flush.currentFrameNumber = ++s.frameNumber;
    flush.safeFrameNumber = s.frameNumber - 1;
    s.context->flush(flush);

    const GLenum glError = glGetError();
    if (glError == GL_OUT_OF_MEMORY) {
        *error = "GL_OUT_OF_MEMORY during Rive flush";
        return false;
    }
    if (glError != GL_NO_ERROR) {
        __android_log_print(ANDROID_LOG_WARN, RIVE_PANEL_LOG_TAG, "GL error 0x%x after Rive flush",
                            glError);
    }
    return true;
}
