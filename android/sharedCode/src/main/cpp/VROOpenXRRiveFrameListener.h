//
//  VROOpenXRRiveFrameListener.h
//  ViroRenderer
//
//  The VRORivePanel::FrameListener that VROSceneRendererOpenXR gives a
//  texture-ring Rive panel. On the panel's worker it posts at most one task at
//  a time to the XR render thread, which then runs onRenderThread with the
//  panel's link (compositor invalidation, fallback update).
//
//  The link is held weakly. The panel owns this listener, and the link owns
//  the scene fallback texture, which owns the panel; a strong capture closes
//  that loop and nothing in it is ever freed (the fallback's GL objects and
//  the panel's worker, ring and EGL context included). With the weak capture
//  the renderer's link map is the link's only owner, and a task that runs
//  after the link was removed does nothing.
//
//  The in-flight flag is shared by the listener and its task, not stored in
//  the link, so the worker never takes a reference to the link and the link is
//  only ever released on the render thread.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

// dispatch: called on the worker with a std::function<void()> to run on the
// render thread (VROPlatformDispatchAsyncRenderer in the renderer).
// onRenderThread: called on the render thread with the live link.
template <typename Link, typename Dispatch, typename OnRenderThread>
std::function<void(uint64_t)> VROMakeRiveFrameListener(std::weak_ptr<Link> link,
                                                       Dispatch dispatch,
                                                       OnRenderThread onRenderThread) {
    auto pending = std::make_shared<std::atomic<bool>>(false);
    return [link = std::move(link), pending, dispatch = std::move(dispatch),
            onRenderThread = std::move(onRenderThread)](uint64_t) {
        if (pending->exchange(true)) return;
        dispatch(std::function<void()>([link, pending, onRenderThread] {
            pending->store(false);
            if (std::shared_ptr<Link> live = link.lock()) onRenderThread(*live);
        }));
    };
}
