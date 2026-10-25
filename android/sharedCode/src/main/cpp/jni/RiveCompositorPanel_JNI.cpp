//
//  RiveCompositorPanel_JNI.cpp
//  ViroRenderer
//
//  JNI for com.viro.core.RiveCompositorPanel: a Rive panel shown as an OpenXR
//  compositor layer (VROSceneRendererOpenXR::addRiveCompositorPanel), with its
//  presentation changes and scene fallback texture reported to Java.
//
//  Threads:
//  - Compositor calls (add, pose, visibility, remove, capabilities, the
//    fallback texture) run on the XR render thread, posted with
//    VROPlatformDispatchAsyncRenderer. They wait while the render loop is
//    paused and are dropped with the queue if the renderer is destroyed.
//  - Rive calls (files, view-model writes, pointers, setActive) go straight
//    to VRORivePanel, which queues them for its own worker from any thread.
//  - Every call into Java runs on the application (main) thread, posted with
//    VROPlatformDispatchAsyncApplication. The Java object is held through a
//    weak global ref that is read and deleted only on that thread, so a task
//    posted after dispose finds it null and does nothing.
//

#include <jni.h>

#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "PersistentRef.h"
#include "RiveCompositorPanelMapping.h"
#include "VRODefines.h"
#include "VROLog.h"
#include "VROPlatformUtil.h"
#include "VRORenderer_JNI.h"
#include "VROSceneRendererOpenXR.h"
#include "VROTexture.h"
#include VRO_C_INCLUDE

#define VRO_METHOD(return_type, method_name) \
    JNIEXPORT return_type JNICALL Java_com_viro_core_RiveCompositorPanel_##method_name

namespace {

// One Java RiveCompositorPanel. Java owns a heap shared_ptr to it (the jlong
// handle); render-thread tasks hold their own copies.
struct __attribute__((visibility("hidden"))) PanelHost {
    std::weak_ptr<VROSceneRendererOpenXR> renderer;
    std::string id;
    // Written once on the render thread before Java hears onNativeCreated,
    // through the application queue; never reset, so Java threads read it
    // without a lock afterwards. Calls on a disposed VRORivePanel are refused.
    std::shared_ptr<VRORivePanel> panel;
    // Application thread only after construction.
    jweak javaPanel = nullptr;
    // Render thread only.
    bool fallbackDelivered = false;
    std::atomic<bool> disposed{false};
};

// Panels of the one OpenXR renderer, by id. Render thread only. The renderer
// keeps a single host presentation listener, so this dispatches to panels.
struct __attribute__((visibility("hidden"))) PanelRegistry {
    std::weak_ptr<VROSceneRendererOpenXR> renderer;
    std::map<std::string, std::weak_ptr<PanelHost>> hosts;
};

std::shared_ptr<PanelRegistry> &registry() {
    static std::shared_ptr<PanelRegistry> instance;
    return instance;
}

std::shared_ptr<PanelHost> *handle(jlong ref) {
    return reinterpret_cast<std::shared_ptr<PanelHost> *>(ref);
}

std::shared_ptr<PanelHost> hostFor(jlong ref) {
    return ref == 0 ? nullptr : *handle(ref);
}

// Application thread. Calls `method` on the Java panel if it is still alive.
template <typename... Args>
void callJava(const std::shared_ptr<PanelHost> &host, const char *method, const char *signature,
              Args... args) {
    if (host->javaPanel == nullptr) return;
    JNIEnv *env = VROPlatformGetJNIEnv();
    jobject panel = env->NewLocalRef(host->javaPanel);
    if (panel == nullptr) return;
    VROPlatformCallHostFunction(panel, method, signature, args...);
    env->DeleteLocalRef(panel);
}

void postCreateFailed(const std::shared_ptr<PanelHost> &host, int32_t code,
                      const std::string &message) {
    VROPlatformDispatchAsyncApplication([host, code, message] {
        JNIEnv *env = VROPlatformGetJNIEnv();
        jstring jmessage = env->NewStringUTF(message.c_str());
        callJava(host, "onNativeCreateFailed", "(ILjava/lang/String;)V", (jint) code, jmessage);
        env->DeleteLocalRef(jmessage);
    });
}

// Render thread, at the start of the frame after a presentation change.
void onPresentation(const std::weak_ptr<PanelRegistry> &weakRegistry, const std::string &id,
                    VROOpenXRPanelPresentation presentation,
                    VROOpenXRPanelEvictionReason reason) {
    std::shared_ptr<PanelRegistry> panels = weakRegistry.lock();
    if (!panels) return;
    auto it = panels->hosts.find(id);
    if (it == panels->hosts.end()) return;
    std::shared_ptr<PanelHost> host = it->second.lock();
    std::shared_ptr<VROSceneRendererOpenXR> renderer = panels->renderer.lock();
    if (!host || !renderer || host->disposed.load()) return;

    // The renderer builds the fallback on the first eviction, before this
    // runs. Java gets one handle to it and keeps it; later evictions reuse it.
    jlong textureRef = 0;
    if (presentation == VROOpenXRPanelPresentation::Evicted && !host->fallbackDelivered) {
        std::shared_ptr<VROTexture> fallback = renderer->getRiveCompositorPanelFallbackTexture(id);
        if (fallback) {
            textureRef = VRO_REF_NEW(VROTexture, fallback);
            host->fallbackDelivered = true;
        }
    }
    const jint presentationCode = RiveCompositorPanelMapping::presentationCode(presentation);
    const jint reasonCode = RiveCompositorPanelMapping::evictionReasonCode(reason);
    VROPlatformDispatchAsyncApplication([host, presentationCode, reasonCode, textureRef] {
        if (host->javaPanel == nullptr) {
            if (textureRef != 0) VRO_REF_DELETE(VROTexture, textureRef);
            return;
        }
        callJava(host, "onNativePresentation", "(IIJ)V", presentationCode, reasonCode,
                 textureRef);
    });
}

// Render thread. Points the renderer's host listener at the registry for
// this renderer, starting a new registry when the renderer changed.
std::shared_ptr<PanelRegistry> registryFor(const std::shared_ptr<VROSceneRendererOpenXR> &renderer) {
    std::shared_ptr<PanelRegistry> &panels = registry();
    if (!panels || panels->renderer.lock() != renderer) {
        panels = std::make_shared<PanelRegistry>();
        panels->renderer = renderer;
        std::weak_ptr<PanelRegistry> weakRegistry = panels;
        renderer->setCompositorPanelPresentationListener(
            [weakRegistry](const std::string &id, VROOpenXRPanelPresentation presentation,
                           VROOpenXRPanelEvictionReason reason) {
                onPresentation(weakRegistry, id, presentation, reason);
            });
    }
    return panels;
}

void unregister(const std::shared_ptr<VROSceneRendererOpenXR> &renderer, const std::string &id) {
    std::shared_ptr<PanelRegistry> &panels = registry();
    if (!panels || panels->renderer.lock() != renderer) return;
    panels->hosts.erase(id);
    if (panels->hosts.empty()) {
        renderer->setCompositorPanelPresentationListener(nullptr);
        panels.reset();
    }
}

std::shared_ptr<VROSceneRendererOpenXR> openXRRenderer(jlong rendererRef) {
    return std::dynamic_pointer_cast<VROSceneRendererOpenXR>(Renderer::native(rendererRef));
}

bool readFloats(JNIEnv *env, jfloatArray array, float *out, jsize count) {
    if (array == nullptr || env->GetArrayLength(array) != count) return false;
    env->GetFloatArrayRegion(array, 0, count, out);
    return !env->ExceptionCheck();
}

}  // namespace

extern "C" {

// Returns the handle Java keeps, or 0 when the renderer is not an OpenXR
// renderer. Failures after that arrive through onNativeCreateFailed.
VRO_METHOD(jlong, nativeCreate)(JNIEnv *env, jobject obj, jlong rendererRef, jstring jid,
                                jint shapeCode, jfloatArray jposition, jfloatArray jorientation,
                                jfloat widthM, jfloat heightM, jfloat radiusM, jint widthPx,
                                jint heightPx, jboolean visible, jfloat cornerRadiusM,
                                jint fitCode, jfloat alignmentX, jfloat alignmentY,
                                jint clearColor, jfloat maxFrameRateHz, jbyteArray jfile,
                                jstring jartboard, jstring jstateMachine) {
    std::shared_ptr<VROSceneRendererOpenXR> renderer = openXRRenderer(rendererRef);
    if (!renderer) return 0;

    auto host = std::make_shared<PanelHost>();
    host->renderer = renderer;
    host->id = VRO_STRING_STL(jid);
    host->javaPanel = env->NewWeakGlobalRef(obj);

    VROOpenXRPanelDesc desc;
    desc.id = host->id;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    VRORivePanelOptions options;
    bool valid = RiveCompositorPanelMapping::shapeFromCode(shapeCode, &desc.shape) &&
                 RiveCompositorPanelMapping::fitFromCode(fitCode, &options.fit) &&
                 readFloats(env, jposition, position, 3) &&
                 readFloats(env, jorientation, orientation, 4);
    desc.pose.position = {position[0], position[1], position[2]};
    desc.pose.orientation = {orientation[0], orientation[1], orientation[2], orientation[3]};
    desc.widthMeters = widthM;
    desc.heightMeters = heightM;
    desc.radiusMeters = radiusM;
    // The compositor checks the 16..4096 range; negative values must not wrap.
    desc.widthPixels = widthPx > 0 ? (uint32_t) widthPx : 0;
    desc.heightPixels = heightPx > 0 ? (uint32_t) heightPx : 0;
    desc.visible = visible;
    options.alignmentX = alignmentX;
    options.alignmentY = alignmentY;
    options.clearColor = (uint32_t) clearColor;
    options.maxFrameRateHz = maxFrameRateHz;

    std::vector<uint8_t> file;
    if (jfile != nullptr) {
        file.resize((size_t) env->GetArrayLength(jfile));
        env->GetByteArrayRegion(jfile, 0, (jsize) file.size(),
                                reinterpret_cast<jbyte *>(file.data()));
    }
    const std::string artboard = VRO_STRING_STL(jartboard);
    const std::string stateMachine = VRO_STRING_STL(jstateMachine);
    jlong ref = reinterpret_cast<jlong>(new std::shared_ptr<PanelHost>(host));

    if (!valid || !std::isfinite(cornerRadiusM) || cornerRadiusM < 0.0f) {
        // Java checks these too; this keeps a bad call from reaching the GPU.
        postCreateFailed(host, RiveCompositorPanelMapping::createErrorCode(
                                   VROOpenXRPanelError::InvalidDimensions),
                         "invalid shape, fit, pose array or corner radius");
        return ref;
    }

    std::weak_ptr<PanelHost> weakHost = host;
    VRORivePanel::StatusListener status = [weakHost](VRORivePanelState state,
                                                     const std::string &detail) {
        std::shared_ptr<PanelHost> strong = weakHost.lock();
        if (!strong) return;
        const jint code = RiveCompositorPanelMapping::stateCode(state);
        VROPlatformDispatchAsyncApplication([strong, code, detail] {
            JNIEnv *env = VROPlatformGetJNIEnv();
            jstring jdetail = env->NewStringUTF(detail.c_str());
            callJava(strong, "onNativeStatus", "(ILjava/lang/String;)V", code, jdetail);
            env->DeleteLocalRef(jdetail);
        });
    };

    VROPlatformDispatchAsyncRenderer([host, desc, cornerRadiusM, options, status,
                                      file = std::move(file), artboard, stateMachine]() mutable {
        std::shared_ptr<VROSceneRendererOpenXR> renderer = host->renderer.lock();
        if (!renderer || host->disposed.load()) {
            postCreateFailed(host, RiveCompositorPanelMapping::createErrorCode(
                                       VROOpenXRPanelError::SessionUnavailable),
                             VROOpenXRPanelErrorMessage(VROOpenXRPanelError::SessionUnavailable));
            return;
        }
        std::shared_ptr<VRORivePanel> panel;
        VROOpenXRPanelError error =
            renderer->addRiveCompositorPanel(desc, cornerRadiusM, options, status, &panel);
        if (error != VROOpenXRPanelError::None) {
            postCreateFailed(host, RiveCompositorPanelMapping::createErrorCode(error),
                             VROOpenXRPanelErrorMessage(error));
            return;
        }
        // select* before loadFile: the file instantiates the latest selection.
        panel->selectArtboard(artboard);
        panel->selectStateMachine(stateMachine);
        if (!panel->loadFile(file.data(), file.size())) {
            renderer->removeCompositorPanel(desc.id);
            VROPlatformDispatchAsyncBackground([panel] { panel->dispose(); });
            postCreateFailed(host, RiveCompositorPanelMapping::kInvalidRiveFile,
                             "the .riv file is empty or larger than 64 MiB");
            return;
        }
        host->panel = panel;
        registryFor(renderer)->hosts[desc.id] = host;
        VROPlatformDispatchAsyncApplication(
            [host] { callJava(host, "onNativeCreated", "()V"); });
    });
    return ref;
}

// Removes the layer, then frees the Rive worker off the render thread, and
// releases Java's handle. The ring and the shared context outlive the
// swapchain: removeCompositorPanel destroys the swapchain first, and a
// fallback texture Java still holds keeps the panel object alive.
VRO_METHOD(void, nativeDispose)(JNIEnv *env, jobject obj, jlong ref) {
    std::shared_ptr<PanelHost> *heap = handle(ref);
    if (heap == nullptr) return;
    std::shared_ptr<PanelHost> host = *heap;
    delete heap;
    host->disposed.store(true);

    VROPlatformDispatchAsyncRenderer([host] {
        std::shared_ptr<VROSceneRendererOpenXR> renderer = host->renderer.lock();
        if (renderer) {
            renderer->removeCompositorPanel(host->id);
            unregister(renderer, host->id);
        }
        if (std::shared_ptr<VRORivePanel> panel = host->panel) {
            VROPlatformDispatchAsyncBackground([panel] { panel->dispose(); });
        }
    });
    // Queued behind every Java call already posted for this panel.
    VROPlatformDispatchAsyncApplication([host] {
        if (host->javaPanel != nullptr) {
            VROPlatformGetJNIEnv()->DeleteWeakGlobalRef(host->javaPanel);
            host->javaPanel = nullptr;
        }
    });
}

VRO_METHOD(void, nativeSetPose)(JNIEnv *env, jobject obj, jlong ref, jfloatArray jposition,
                                jfloatArray jorientation) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    float p[3];
    float q[4];
    if (!host || !readFloats(env, jposition, p, 3) || !readFloats(env, jorientation, q, 4)) return;
    XrPosef pose;
    pose.position = {p[0], p[1], p[2]};
    pose.orientation = {q[0], q[1], q[2], q[3]};
    VROPlatformDispatchAsyncRenderer([host, pose] {
        std::shared_ptr<VROSceneRendererOpenXR> renderer = host->renderer.lock();
        if (renderer && !host->disposed.load() &&
            !renderer->updateCompositorPanelPose(host->id, pose)) {
            pwarn("RiveCompositorPanel %s: pose rejected", host->id.c_str());
        }
    });
}

VRO_METHOD(void, nativeSetVisible)(JNIEnv *env, jobject obj, jlong ref, jboolean visible) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    if (!host) return;
    const bool show = visible;
    VROPlatformDispatchAsyncRenderer([host, show] {
        std::shared_ptr<VROSceneRendererOpenXR> renderer = host->renderer.lock();
        if (renderer && !host->disposed.load()) {
            renderer->setCompositorPanelVisible(host->id, show);
        }
    });
}

VRO_METHOD(jboolean, nativeSetNumber)(JNIEnv *env, jobject obj, jlong ref, jstring path,
                                      jfloat value) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    return host && host->panel && host->panel->setNumber(VRO_STRING_STL(path), value);
}

VRO_METHOD(jboolean, nativeSetBoolean)(JNIEnv *env, jobject obj, jlong ref, jstring path,
                                       jboolean value) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    return host && host->panel && host->panel->setBoolean(VRO_STRING_STL(path), value);
}

VRO_METHOD(jboolean, nativeSetString)(JNIEnv *env, jobject obj, jlong ref, jstring path,
                                      jstring value) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    return host && host->panel &&
           host->panel->setString(VRO_STRING_STL(path), VRO_STRING_STL(value));
}

VRO_METHOD(jboolean, nativeFireTrigger)(JNIEnv *env, jobject obj, jlong ref, jstring path) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    return host && host->panel && host->panel->fireTrigger(VRO_STRING_STL(path));
}

// action: 0 down, 1 move, 2 up (RiveCompositorPanel.POINTER_*).
VRO_METHOD(jboolean, nativePointer)(JNIEnv *env, jobject obj, jlong ref, jint action, jfloat xPx,
                                    jfloat yPx, jint pointerId) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    if (!host || !host->panel) return false;
    switch (action) {
        case 0: return host->panel->pointerDown(xPx, yPx, pointerId);
        case 1: return host->panel->pointerMove(xPx, yPx, pointerId);
        case 2: return host->panel->pointerUp(xPx, yPx, pointerId);
        default: return false;
    }
}

VRO_METHOD(jboolean, nativeSetActive)(JNIEnv *env, jobject obj, jlong ref, jboolean active) {
    std::shared_ptr<PanelHost> host = hostFor(ref);
    return host && host->panel && host->panel->setActive(active);
}

// Answers on the application thread through
// RiveCompositorPanel.CapabilitiesCallback.onNativeCapabilities.
VRO_METHOD(void, nativeGetCapabilities)(JNIEnv *env, jclass clazz, jlong rendererRef,
                                        jobject jcallback) {
    jobject callback = env->NewGlobalRef(jcallback);
    std::weak_ptr<VROSceneRendererOpenXR> weakRenderer = openXRRenderer(rendererRef);
    auto deliver = [callback](VROOpenXRCompositorPanelCapabilities capabilities) {
        VROPlatformDispatchAsyncApplication([callback, capabilities] {
            VROPlatformCallHostFunction(callback, "onNativeCapabilities", "(ZZIZ)V",
                                        (jboolean) capabilities.sessionReady,
                                        (jboolean) capabilities.cylinder,
                                        (jint) capabilities.maxPanels,
                                        (jboolean) capabilities.rive);
            VROPlatformGetJNIEnv()->DeleteGlobalRef(callback);
        });
    };
    if (weakRenderer.expired()) {
        deliver({});
        return;
    }
    VROPlatformDispatchAsyncRenderer([weakRenderer, deliver] {
        std::shared_ptr<VROSceneRendererOpenXR> renderer = weakRenderer.lock();
        deliver(renderer ? renderer->getCompositorPanelCapabilities()
                         : VROOpenXRCompositorPanelCapabilities{});
    });
}

}  // extern "C"
