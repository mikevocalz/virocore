// VROSceneRendererOpenXR.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROSceneRendererOpenXR.h"

#include <android/log.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <algorithm>
#include <string>

#include "VRORendererConfiguration.h"
#include "VRODriverOpenGLAndroidOpenXR.h"
#include "VRODisplayOpenGLOpenXR.h"
#include "VROInputControllerOpenXR.h"
#include "VROARSessionOpenXR.h"
#include "VROARScene.h"
#include "VROSceneController.h"
#include "VROLog.h"
#include "VROAllocationTracker.h"
#include "VROTime.h"
#include "VROThreadRestricted.h"
#include "VROPlatformUtil.h"

#define LOG_TAG "VRORendererOpenXR"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

// ──────────────────────────────────────────────────────────────────────────────
// Required extensions
// ──────────────────────────────────────────────────────────────────────────────

// Hard requirements: every conformant OpenXR runtime exposes these. If one is
// missing we genuinely cannot run, on any device.
static const char *const kHardRequiredExtensions[] = {
    XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,   // GLES context binding (mandatory)
};
static constexpr uint32_t kHardRequiredExtensionCount =
    sizeof(kHardRequiredExtensions) / sizeof(kHardRequiredExtensions[0]);

// Soft requirements: required on Meta's runtime, but enabled iff the active
// runtime actually enumerates them. PICO OS 6 advertises android_create_instance
// but is stricter than Meta about the chained XrInstanceCreateInfoAndroidKHR;
// we only chain that struct when the extension is genuinely enabled.
static const char *const kSoftRequiredExtensions[] = {
    XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
};
static constexpr uint32_t kSoftRequiredExtensionCount =
    sizeof(kSoftRequiredExtensions) / sizeof(kSoftRequiredExtensions[0]);

static const char *const kOptionalExtensions[] = {
    XR_FB_PASSTHROUGH_EXTENSION_NAME,           // mixed reality (Quest 2 BW, Quest 3 color)
    XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,  // 90 / 120 Hz mode
    XR_EXT_HAND_TRACKING_EXTENSION_NAME,        // M3: skeletal hand tracking (26 joints)
    XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME,     // M3: aim pose + pinch strength from runtime
    XR_EXT_PLANE_DETECTION_EXTENSION_NAME,      // M5: real-time plane detection (cross-vendor)
    XR_FB_SCENE_EXTENSION_NAME,                 // M5: Meta room model (Space Setup) — bbox/boundary/labels
    XR_FB_SPATIAL_ENTITY_EXTENSION_NAME,        // M5: spatial entity components
    XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME,  // M5: query stored room entities
    XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME, // eye-gaze ray as an onHover source (Quest Pro only)
    XR_FB_FOVEATION_EXTENSION_NAME,                   // foveated rendering (fill-rate win on high-PPD)
    XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,     // foveation level / area / dynamic config
    XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,      // apply foveation profile to live swapchain
    "XR_META_foveation_eye_tracked",                  // gaze-driven foveation (perm-gated; flagged only)
    XR_FB_SPACE_WARP_EXTENSION_NAME,                  // ASW motion-vector reprojection (flagged; loop TODO)
    XR_EXT_LOCAL_FLOOR_EXTENSION_NAME,                // floor-level reference space (PICO 4 Ultra; OpenXR 1.1 core)
};

// PICO (ByteDance) controller-interaction extensions. These gate the
// /interaction_profiles/bytedance/* binding paths used in VROInputControllerOpenXR;
// absent on Meta runtimes (skipped), present on PICO (enabled so the input
// layer's PICO profile suggestions resolve). String literals avoid a header
// dependency that may predate these vendor extensions.
static const char *const kPicoControllerExtensions[] = {
    "XR_BD_controller_interaction",        // Neo3 / 4 / 4 Pro / G3 profiles
    "XR_BD_ultra_controller_interaction",  // PICO 4 Ultra (pico4s) profile
};
static constexpr uint32_t kPicoControllerExtensionCount =
    sizeof(kPicoControllerExtensions) / sizeof(kPicoControllerExtensions[0]);

// ──────────────────────────────────────────────────────────────────────────────
// Utility macros
// ──────────────────────────────────────────────────────────────────────────────

#define XR_CHECK(call)                                                           \
    do {                                                                         \
        XrResult _r = (call);                                                    \
        if (XR_FAILED(_r)) {                                                     \
            ALOGE("OpenXR call failed at %s:%d  result=%d  expr: %s",           \
                  __FILE__, __LINE__, (int)_r, #call);                           \
        }                                                                        \
    } while (0)

#define XR_RETURN_FALSE(call)                                                    \
    do {                                                                         \
        XrResult _r = (call);                                                    \
        if (XR_FAILED(_r)) {                                                     \
            ALOGE("OpenXR fatal at %s:%d  result=%d  expr: %s",                 \
                  __FILE__, __LINE__, (int)_r, #call);                           \
            return false;                                                        \
        }                                                                        \
    } while (0)

// ──────────────────────────────────────────────────────────────────────────────
// Conversion helpers
// ──────────────────────────────────────────────────────────────────────────────

static VROMatrix4f xrPoseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f rot = q.getMatrix();
    rot[12] = pose.position.x;
    rot[13] = pose.position.y;
    rot[14] = pose.position.z;
    return rot;
}

// Compute a projection matrix from OpenXR FoV angles (tangent half-angles).
static VROMatrix4f xrFovToProjection(const XrFovf &fov,
                                      float nearZ = 0.1f,
                                      float farZ  = 1000.0f) {
    const float left   = tanf(fov.angleLeft);
    const float right  = tanf(fov.angleRight);
    const float down   = tanf(fov.angleDown);
    const float up     = tanf(fov.angleUp);

    const float w  =  right - left;
    const float h  =  up    - down;
    const float q  = -(farZ + nearZ) / (farZ - nearZ);
    const float qn = -2.0f * farZ * nearZ / (farZ - nearZ);

    float m[16] = {};
    m[0]  =  2.0f / w;
    m[5]  =  2.0f / h;
    m[8]  =  (right + left) / w;
    m[9]  =  (up    + down) / h;
    m[10] =  q;
    m[11] = -1.0f;
    m[14] =  qn;
    return VROMatrix4f(m);
}

// ──────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ──────────────────────────────────────────────────────────────────────────────

VROSceneRendererOpenXR::VROSceneRendererOpenXR(VRORendererConfiguration config,
                                               std::shared_ptr<gvr::AudioApi> gvrAudio,
                                               jobject view, jobject activity, JNIEnv *env)
    : _gvrAudio(gvrAudio)
{
    // Hold global refs so these objects live as long as the renderer
    _activity = env->NewGlobalRef(activity);
    _jview    = env->NewGlobalRef(view);
    env->GetJavaVM(&_jvm);

    if (!initOpenXR()) {
        ALOGE("initOpenXR() failed — Quest renderer will not function");
        return;
    }
    if (!createEGLContext()) {
        ALOGE("createEGLContext() failed");
        return;
    }
    if (!createSession()) {
        ALOGE("createSession() failed");
        return;
    }

    // Create the OpenXR driver and assign it to both the derived-class field
    // (for getOpenXRDisplay()) and the base-class _driver (for setSceneController etc.)
    _openxrDriver = std::make_shared<VRODriverOpenGLAndroidOpenXR>(gvrAudio);
    _driver = _openxrDriver;  // base class std::shared_ptr<VRODriverOpenGLAndroid>
    _inputController = std::make_shared<VROInputControllerOpenXR>(_openxrDriver);
    _inputController->createActionSet(_instance, _session, _eyeGazeSupported);
    initHandTracking();  // no-op if XR_EXT_hand_tracking not available on this device

    // Wire the B/Menu button back to Android's back-press so React Native's
    // BackHandler fires in VRActivity. The callback runs on the render thread;
    // ViroViewOpenXR.onNativeBackButton() posts to the UI thread internally.
    {
        JavaVM *jvm = _jvm;
        jobject jview = _jview;  // global ref — safe to capture
        _inputController->setBackButtonCallback([jvm, jview]() {
            JNIEnv *env = nullptr;
            bool attached = false;
            if (jvm->GetEnv((void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
                jvm->AttachCurrentThread(&env, nullptr);
                attached = true;
            }
            if (env) {
                jclass cls = env->GetObjectClass(jview);
                jmethodID mid = env->GetMethodID(cls, "onNativeBackButton", "()V");
                env->DeleteLocalRef(cls);
                if (mid) env->CallVoidMethod(jview, mid);
            }
            if (attached) jvm->DetachCurrentThread();
        });
    }

    // Create the shared VRORenderer and wire it to the input controller.
    // VROSceneRenderer::_renderer is null until explicitly set here — every other
    // platform (GVR, OVR) does the equivalent in their constructor.
    _renderer = std::make_shared<VRORenderer>(config, _inputController);

    // OpenXR owns its own render thread — bypass the GLSurfaceView dispatcher.
    // VROPlatformDrainRendererQueue() is called at the top of each renderFrame().
    VROPlatformSetUseDirectRendererQueue(true);

    // Release the EGL context from the main thread so the render thread can
    // take exclusive ownership via eglMakeCurrent in renderLoop().
    // EGL contexts can only be current on one thread at a time.
    eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    ALOGV("VROSceneRendererOpenXR constructed successfully");
}

VROSceneRendererOpenXR::~VROSceneRendererOpenXR() {
    onDestroy();
}

// ──────────────────────────────────────────────────────────────────────────────
// OpenXR initialisation
// ──────────────────────────────────────────────────────────────────────────────

bool VROSceneRendererOpenXR::initOpenXR() {
    // ── Initialize loader (MUST be first OpenXR call on Android) ─────────────
    // Without this the loader cannot locate the Meta Quest runtime and
    // xrEnumerateInstanceExtensionProperties returns 0 extensions.
    {
        PFN_xrInitializeLoaderKHR pfnInitLoader = nullptr;
        xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                              (PFN_xrVoidFunction *)&pfnInitLoader);
        if (!pfnInitLoader) {
            ALOGE("xrInitializeLoaderKHR not available — OpenXR loader too old?");
            return false;
        }
        XrLoaderInitInfoAndroidKHR loaderInfo = {
            XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR
        };
        loaderInfo.applicationVM      = _jvm;
        loaderInfo.applicationContext = _activity;
        XrResult loaderResult = pfnInitLoader(
            (const XrLoaderInitInfoBaseHeaderKHR *)&loaderInfo);
        if (XR_FAILED(loaderResult)) {
            ALOGE("xrInitializeLoaderKHR failed: %d", (int)loaderResult);
            return false;
        }
        ALOGV("xrInitializeLoaderKHR OK");
    }

    // ── Enumerate available extensions ────────────────────────────────────────
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> availableExts(extCount,
        { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount,
                                            availableExts.data());

    // Verify HARD-required extensions are present. Missing => unrunnable.
    for (uint32_t i = 0; i < kHardRequiredExtensionCount; ++i) {
        bool found = false;
        for (auto &ext : availableExts) {
            if (strcmp(ext.extensionName, kHardRequiredExtensions[i]) == 0) {
                found = true; break;
            }
        }
        if (!found) {
            ALOGE("Required OpenXR extension not available: %s",
                  kHardRequiredExtensions[i]);
            return false;
        }
    }

    // Build the extension list: hard-required + soft-required-if-present + optionals-if-present.
    std::vector<const char *> enabledExts(kHardRequiredExtensions,
                                           kHardRequiredExtensions + kHardRequiredExtensionCount);
    bool androidCreateInstanceEnabled = false;
    for (uint32_t i = 0; i < kSoftRequiredExtensionCount; ++i) {
        for (auto &ext : availableExts) {
            if (strcmp(ext.extensionName, kSoftRequiredExtensions[i]) == 0) {
                enabledExts.push_back(kSoftRequiredExtensions[i]);
                if (strcmp(kSoftRequiredExtensions[i],
                           XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) == 0) {
                    androidCreateInstanceEnabled = true;
                }
                ALOGV("Soft-required extension enabled: %s", kSoftRequiredExtensions[i]);
                break;
            }
        }
    }
    for (auto *optExt : kOptionalExtensions) {
        for (auto &ext : availableExts) {
            if (strcmp(ext.extensionName, optExt) == 0) {
                enabledExts.push_back(optExt);
                ALOGV("Optional extension enabled: %s", optExt);
                if (strcmp(optExt, XR_EXT_HAND_TRACKING_EXTENSION_NAME) == 0)
                    _handTrackingAvailable = true;
                if (strcmp(optExt, XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME) == 0)
                    _handAimExtAvailable = true;
                if (strcmp(optExt, XR_EXT_PLANE_DETECTION_EXTENSION_NAME) == 0)
                    _planeDetectionAvailable = true;
                if (strcmp(optExt, XR_FB_SCENE_EXTENSION_NAME) == 0)
                    _fbSceneAvailable = true;
                if (strcmp(optExt, XR_FB_SPATIAL_ENTITY_EXTENSION_NAME) == 0)
                    _fbSpatialEntityAvailable = true;
                if (strcmp(optExt, XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME) == 0)
                    _fbSpatialQueryAvailable = true;
                if (strcmp(optExt, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) == 0)
                    _eyeGazeAvailable = true;
                if (strcmp(optExt, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0)
                    _runtimeInfo.passthroughAvailable = true;
                if (strcmp(optExt, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0)
                    _runtimeInfo.displayRefreshRateAvailable = true;
                if (strcmp(optExt, XR_FB_FOVEATION_EXTENSION_NAME) == 0)
                    _foveationAvailable = true;
                if (strcmp(optExt, "XR_META_foveation_eye_tracked") == 0)
                    _eyeTrackedFoveationAvailable = true;
                if (strcmp(optExt, XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME) == 0)
                    _swapchainUpdateStateAvailable = true;
                if (strcmp(optExt, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME) == 0)
                    _localFloorAvailable = true;
                break;
            }
        }
    }
    // PICO controller-interaction extensions (enable-if-enumerated).
    for (uint32_t i = 0; i < kPicoControllerExtensionCount; ++i) {
        for (auto &ext : availableExts) {
            if (strcmp(ext.extensionName, kPicoControllerExtensions[i]) == 0) {
                enabledExts.push_back(kPicoControllerExtensions[i]);
                ALOGV("PICO controller extension enabled: %s", kPicoControllerExtensions[i]);
                break;
            }
        }
    }

    // ── Create XrInstance ─────────────────────────────────────────────────────
    XrInstanceCreateInfoAndroidKHR androidInfo = {
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR
    };
    androidInfo.applicationActivity = _activity;
    androidInfo.applicationVM       = _jvm;

    XrApplicationInfo appInfo = {};
    strncpy(appInfo.applicationName, "ViroReact", XR_MAX_APPLICATION_NAME_SIZE);
    appInfo.applicationVersion = 1;
    strncpy(appInfo.engineName, "ViroRenderer", XR_MAX_ENGINE_NAME_SIZE);
    appInfo.engineVersion      = 1;
    appInfo.apiVersion         = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    // Only chain XrInstanceCreateInfoAndroidKHR when the extension is enabled.
    // A runtime that did not enumerate it (or rejects the chain, as strict PICO
    // firmware can) must not see a struct referencing it.
    createInfo.next                    = androidCreateInstanceEnabled
                                             ? (const void *)&androidInfo
                                             : nullptr;
    createInfo.applicationInfo         = appInfo;
    createInfo.enabledExtensionCount   = (uint32_t)enabledExts.size();
    createInfo.enabledExtensionNames   = enabledExts.data();

    XrResult createResult = xrCreateInstance(&createInfo, &_instance);
    if (XR_FAILED(createResult)) {
        // xrResultToString needs a valid instance, which we don't have here.
        // The numeric code cross-references against openxr.h's XrResult enum.
        ALOGE("xrCreateInstance failed: XrResult=%d "
              "(androidCreateInstanceEnabled=%d, enabledExtensionCount=%u)",
              (int)createResult, (int)androidCreateInstanceEnabled,
              createInfo.enabledExtensionCount);
        return false;
    }
    ALOGV("xrCreateInstance OK");

    // ── Identify the runtime we bound to ──────────────────────────────────────
    _runtimeInfo.androidCreateInstanceEnabled = androidCreateInstanceEnabled;
    _runtimeInfo.handTrackingAvailable        = _handTrackingAvailable;
    _runtimeInfo.handAimExtAvailable          = _handAimExtAvailable;
    {
        XrInstanceProperties instanceProps = { XR_TYPE_INSTANCE_PROPERTIES };
        if (XR_SUCCEEDED(xrGetInstanceProperties(_instance, &instanceProps))) {
            strncpy(_runtimeInfo.runtimeName, instanceProps.runtimeName,
                    XR_MAX_RUNTIME_NAME_SIZE - 1);
            _runtimeInfo.apiMajor = XR_VERSION_MAJOR(instanceProps.runtimeVersion);
            _runtimeInfo.apiMinor = XR_VERSION_MINOR(instanceProps.runtimeVersion);
            _runtimeInfo.apiPatch = XR_VERSION_PATCH(instanceProps.runtimeVersion);
            std::string lower(_runtimeInfo.runtimeName);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("pico") != std::string::npos ||
                lower.find("apxr") != std::string::npos) {
                _runtimeInfo.vendor = VROOpenXRVendor::PICO;
            } else if (lower.find("oculus") != std::string::npos ||
                       lower.find("meta")   != std::string::npos) {
                _runtimeInfo.vendor = VROOpenXRVendor::META;
            } else {
                _runtimeInfo.vendor = VROOpenXRVendor::KHRONOS_OTHER;
            }
            ALOGI("OpenXR runtime: \"%s\" v%u.%u.%u vendor=%d "
                  "(passthrough=%d refreshRate=%d handTracking=%d foveation=%d)",
                  _runtimeInfo.runtimeName,
                  _runtimeInfo.apiMajor, _runtimeInfo.apiMinor, _runtimeInfo.apiPatch,
                  (int)_runtimeInfo.vendor,
                  (int)_runtimeInfo.passthroughAvailable,
                  (int)_runtimeInfo.displayRefreshRateAvailable,
                  (int)_runtimeInfo.handTrackingAvailable,
                  (int)_foveationAvailable);
        }
        _runtimeInfo.valid = true;
    }

    // ── Get system (HMD) ──────────────────────────────────────────────────────
    XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult sysResult = xrGetSystem(_instance, &sysInfo, &_systemId);
    if (XR_FAILED(sysResult)) {
        ALOGE("xrGetSystem failed: XrResult=%d (runtime=\"%s\")",
              (int)sysResult, _runtimeInfo.runtimeName);
        return false;
    }
    ALOGV("xrGetSystem OK  systemId=%llu", (unsigned long long)_systemId);

    // ── Probe eye-gaze support ────────────────────────────────────────────────
    // The extension being present doesn't mean the device has eye-tracking
    // hardware (only Quest Pro does). Query the system properties and gate the
    // eye-gaze input source on supportsEyeGazeInteraction, so it stays a no-op
    // on Quest 2 / 3 / 3S.
    if (_eyeGazeAvailable) {
        XrSystemEyeGazeInteractionPropertiesEXT eyeGazeProps = {
            XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT
        };
        XrSystemProperties systemProps = { XR_TYPE_SYSTEM_PROPERTIES };
        systemProps.next = &eyeGazeProps;
        if (XR_SUCCEEDED(xrGetSystemProperties(_instance, _systemId, &systemProps))) {
            _eyeGazeSupported = (eyeGazeProps.supportsEyeGazeInteraction == XR_TRUE);
        }
        ALOGV("Eye-gaze interaction: extension=%d supported=%d",
              (int)_eyeGazeAvailable, (int)_eyeGazeSupported);
    }

    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// EGL context
// ──────────────────────────────────────────────────────────────────────────────

bool VROSceneRendererOpenXR::createEGLContext() {
    _eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (_eglDisplay == EGL_NO_DISPLAY) {
        ALOGE("eglGetDisplay failed");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(_eglDisplay, &major, &minor)) {
        ALOGE("eglInitialize failed");
        return false;
    }
    ALOGV("EGL %d.%d", major, minor);

    // Manual config selection — same approach as VROSceneRendererOVR to avoid
    // Android's forced MSAA injection via eglChooseConfig.
    const int MAX_CONFIGS = 1024;
    EGLConfig configs[MAX_CONFIGS];
    EGLint numConfigs = 0;
    eglGetConfigs(_eglDisplay, configs, MAX_CONFIGS, &numConfigs);

    const EGLint wanted[] = {
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };

    for (int i = 0; i < numConfigs; ++i) {
        EGLint val = 0;
        eglGetConfigAttrib(_eglDisplay, configs[i], EGL_RENDERABLE_TYPE, &val);
        if (!(val & EGL_OPENGL_ES3_BIT_KHR)) continue;

        eglGetConfigAttrib(_eglDisplay, configs[i], EGL_SURFACE_TYPE, &val);
        if (!(val & EGL_PBUFFER_BIT)) continue;

        bool match = true;
        for (int j = 0; wanted[j] != EGL_NONE; j += 2) {
            eglGetConfigAttrib(_eglDisplay, configs[i], wanted[j], &val);
            if (val != wanted[j + 1]) { match = false; break; }
        }
        if (match) {
            _eglConfig = configs[i];
            break;
        }
    }
    if (!_eglConfig) {
        ALOGE("No suitable EGL config found");
        return false;
    }

    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    _eglContext = eglCreateContext(_eglDisplay, _eglConfig, EGL_NO_CONTEXT, ctxAttribs);
    if (_eglContext == EGL_NO_CONTEXT) {
        ALOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }

    // Tiny pbuffer surface so the context is current during session creation
    EGLint pbAttribs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    _eglSurface = eglCreatePbufferSurface(_eglDisplay, _eglConfig, pbAttribs);
    eglMakeCurrent(_eglDisplay, _eglSurface, _eglSurface, _eglContext);

    ALOGV("EGL context created and current");
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Session
// ──────────────────────────────────────────────────────────────────────────────

bool VROSceneRendererOpenXR::createSession() {
    // ── Graphics requirements (MANDATORY before xrCreateSession) ──────────────
    // The OpenXR spec requires xrGetOpenGLESGraphicsRequirementsKHR to be called
    // before xrCreateSession or the runtime returns XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING.
    {
        PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetReqs = nullptr;
        xrGetInstanceProcAddr(_instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                              (PFN_xrVoidFunction *)&pfnGetReqs);
        if (!pfnGetReqs) {
            ALOGE("xrGetOpenGLESGraphicsRequirementsKHR not found");
            return false;
        }
        XrGraphicsRequirementsOpenGLESKHR reqs = {
            XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR
        };
        XrResult r = pfnGetReqs(_instance, _systemId, &reqs);
        if (XR_FAILED(r)) {
            ALOGE("xrGetOpenGLESGraphicsRequirementsKHR failed: %d", (int)r);
            return false;
        }
        ALOGV("GLES requirements: min=%u.%u max=%u.%u",
              XR_VERSION_MAJOR(reqs.minApiVersionSupported),
              XR_VERSION_MINOR(reqs.minApiVersionSupported),
              XR_VERSION_MAJOR(reqs.maxApiVersionSupported),
              XR_VERSION_MINOR(reqs.maxApiVersionSupported));
    }

    // The EGL context must already be current (createEGLContext called first).
    XrGraphicsBindingOpenGLESAndroidKHR binding = {
        XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR
    };
    binding.display = _eglDisplay;
    binding.config  = _eglConfig;
    binding.context = _eglContext;

    XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next      = &binding;
    sessionInfo.systemId  = _systemId;

    XR_RETURN_FALSE(xrCreateSession(_instance, &sessionInfo, &_session));
    ALOGV("xrCreateSession OK");

    if (!createReferenceSpace()) return false;
    if (!createSwapchains())    return false;

    // Try to enable passthrough (optional — graceful degradation if unavailable)
    initPassthrough();

    // Create the Quest MR (AR) session if a plane source is available. Two
    // sources are tried; planes are reported in _appSpace so anchors land in
    // the same world frame as rendered content:
    //   • XR_EXT_plane_detection — cross-vendor real-time (absent on Meta runtime)
    //   • XR_FB_scene + spatial_entity(_query) — Meta room model (Space Setup)
    bool fbSceneUsable = _fbSceneAvailable && _fbSpatialEntityAvailable &&
                         _fbSpatialQueryAvailable;
    if (_planeDetectionAvailable || fbSceneUsable) {
        _arSession = std::make_shared<VROARSessionOpenXR>();
        bool ext = _planeDetectionAvailable &&
                   _arSession->initPlaneDetection(_instance, _session, _appSpace);
        bool fb  = fbSceneUsable &&
                   _arSession->initSceneDetection(_instance, _session, _appSpace);
        if (!ext && !fb) {
            ALOGW("No usable plane source initialised — AR-on-Quest planes disabled");
            _arSession.reset();
        } else {
            ALOGV("AR plane source: EXT=%d FB_scene=%d", (int)ext, (int)fb);
        }
    }

    return true;
}

bool VROSceneRendererOpenXR::initHandTracking() {
    if (!_handTrackingAvailable || !_inputController) return false;
    bool ok = _inputController->initHandTracking(_instance, _session, _handAimExtAvailable);
    ALOGV("Hand tracking init: %s (aim ext: %s)", ok ? "OK" : "FAILED",
          _handAimExtAvailable ? "yes" : "no");
    return ok;
}

// Report whether a reference-space type is enumerated by the runtime this frame.
static bool referenceSpaceEnumerated(XrSession session, XrReferenceSpaceType want) {
    uint32_t count = 0;
    if (XR_FAILED(xrEnumerateReferenceSpaces(session, 0, &count, nullptr)) || count == 0) {
        return false;
    }
    std::vector<XrReferenceSpaceType> spaces(count);
    if (XR_FAILED(xrEnumerateReferenceSpaces(session, count, &count, spaces.data()))) {
        return false;
    }
    for (XrReferenceSpaceType s : spaces) {
        if (s == want) return true;
    }
    return false;
}

bool VROSceneRendererOpenXR::deriveFloorOffset(XrTime time, float *outOffsetY) {
    // Emulate LOCAL_FLOOR (per the XR_EXT_local_floor spec text) by locating the
    // STAGE space against LOCAL: STAGE's origin sits on the physical floor, so
    // its Y in LOCAL is the negative floor height. Requires both spaces and a
    // valid position this frame.
    if (!referenceSpaceEnumerated(_session, XR_REFERENCE_SPACE_TYPE_STAGE) ||
        !referenceSpaceEnumerated(_session, XR_REFERENCE_SPACE_TYPE_LOCAL)) {
        return false;
    }
    XrSpace localSpace = XR_NULL_HANDLE, stageSpace = XR_NULL_HANDLE;
    XrReferenceSpaceCreateInfo info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    info.poseInReferenceSpace = { {0, 0, 0, 1}, {0, 0, 0} };
    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (XR_FAILED(xrCreateReferenceSpace(_session, &info, &localSpace))) {
        return false;
    }
    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (XR_FAILED(xrCreateReferenceSpace(_session, &info, &stageSpace))) {
        xrDestroySpace(localSpace);
        return false;
    }

    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    XrResult r = xrLocateSpace(stageSpace, localSpace, time, &loc);
    xrDestroySpace(stageSpace);
    xrDestroySpace(localSpace);

    if (XR_FAILED(r) || !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        return false;
    }
    // STAGE origin is on the floor; its Y in LOCAL is negative. The floor is
    // that far below the eye-level origin.
    *outOffsetY = -loc.pose.position.y;
    return true;
}

bool VROSceneRendererOpenXR::buildReferenceSpace(VROTrackingOrigin origin,
                                                 XrSpace *outSpace,
                                                 XrReferenceSpaceType *outType) {
    XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.poseInReferenceSpace = { {0, 0, 0, 1}, {0, 0, 0} };  // identity

    if (origin == VROTrackingOrigin::Floor) {
        // Rung 1: native LOCAL_FLOOR when the runtime enumerates it.
        if (_localFloorAvailable &&
            referenceSpaceEnumerated(_session, XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR)) {
            spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
            XrResult r = xrCreateReferenceSpace(_session, &spaceInfo, outSpace);
            if (XR_SUCCEEDED(r)) {
                *outType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
                _floorOffsetY = 0.0f;
                ALOGI("Reference space: LOCAL_FLOOR (native floor origin)");
                return true;
            }
            ALOGW("LOCAL_FLOOR create failed (%d); falling back to STAGE emulation", r);
        }
        // Rung 2: emulate — offset a LOCAL space down to the STAGE floor height.
        float offsetY = 0.0f;
        if (deriveFloorOffset(_lastPredictedDisplayTime, &offsetY)) {
            spaceInfo.referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_LOCAL;
            spaceInfo.poseInReferenceSpace.position.y = offsetY;
            XrResult r = xrCreateReferenceSpace(_session, &spaceInfo, outSpace);
            if (XR_SUCCEEDED(r)) {
                *outType = XR_REFERENCE_SPACE_TYPE_LOCAL;
                _floorOffsetY = offsetY;
                ALOGI("Reference space: LOCAL offset %.3f m (STAGE-emulated floor)", offsetY);
                return true;
            }
            ALOGW("Emulated floor space create failed (%d); staying eye-level", r);
        } else {
            // Rung 3: neither source available. Do not guess a human height —
            // stay eye-level and let the JS layer report the downgrade.
            ALOGW("Floor origin requested but neither LOCAL_FLOOR nor STAGE is "
                  "available; staying eye-level");
        }
    }

    // Eye origin (and every Floor fallthrough): plain LOCAL.
    spaceInfo.referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace = { {0, 0, 0, 1}, {0, 0, 0} };
    XrResult r = xrCreateReferenceSpace(_session, &spaceInfo, outSpace);
    if (XR_FAILED(r)) {
        ALOGE("LOCAL reference space creation failed: %d", r);
        return false;
    }
    *outType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    _floorOffsetY = 0.0f;
    ALOGI("Reference space: LOCAL (eye-level origin)");
    return true;
}

bool VROSceneRendererOpenXR::createReferenceSpace() {
    // Default (Eye) matches every other Viro platform: origin at head level at
    // session start. STAGE placed Y=0 on the floor (~1.6 m below the eye), which
    // dropped content at world (0,0,-2) ~39° below the horizon on Quest 3. Floor
    // origin is opt-in via setTrackingOrigin and resolved by buildReferenceSpace.
    return buildReferenceSpace(_trackingOrigin, &_appSpace, &_appSpaceType);
}

void VROSceneRendererOpenXR::setTrackingOrigin(VROTrackingOrigin origin) {
    if (origin == _trackingOrigin && _appSpace != XR_NULL_HANDLE) {
        return;
    }
    _trackingOrigin = origin;
    // Cached only until session start when there is no live session yet.
    if (_session == XR_NULL_HANDLE) {
        return;
    }
    XrSpace newSpace = XR_NULL_HANDLE;
    XrReferenceSpaceType newType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!buildReferenceSpace(origin, &newSpace, &newType)) {
        return;  // build logged; keep the existing space
    }
    XrSpace old = _appSpace;
    _appSpace     = newSpace;
    _appSpaceType = newType;
    if (old != XR_NULL_HANDLE) {
        xrDestroySpace(old);
    }
}

bool VROSceneRendererOpenXR::createSwapchains() {
    // Enumerate view configuration (stereo: 2 views)
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(_instance, _systemId,
                                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                       0, &viewCount, nullptr);
    if (viewCount != 2) {
        ALOGE("Expected 2 views, got %u", viewCount);
        return false;
    }
    std::vector<XrViewConfigurationView> viewConfigs(viewCount,
        { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrEnumerateViewConfigurationViews(_instance, _systemId,
                                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                       viewCount, &viewCount, viewConfigs.data());

    // Choose swapchain format: prefer sRGB, fall back to RGBA8
    uint32_t fmtCount = 0;
    xrEnumerateSwapchainFormats(_session, 0, &fmtCount, nullptr);
    std::vector<int64_t> formats(fmtCount);
    xrEnumerateSwapchainFormats(_session, fmtCount, &fmtCount, formats.data());

    int64_t chosenFormat = GL_RGBA8;
    for (int64_t f : formats) {
        if (f == GL_SRGB8_ALPHA8_EXT) { chosenFormat = f; break; }
    }
    ALOGI("Swapchain format: 0x%llx (%s)", (long long)chosenFormat,
          chosenFormat == GL_SRGB8_ALPHA8_EXT ? "sRGB" : "linear RGBA8");

    // The colour mode must track the format actually chosen, not a constant: an
    // sRGB swapchain encodes gamma on write (render Linear), a plain RGBA8 one
    // does not (render NonLinear, else the image is too dark). PICO firmware
    // that enumerates no sRGB format takes the RGBA8 branch.
    if (_openxrDriver) {
        _openxrDriver->setColorRenderingMode(chosenFormat == GL_SRGB8_ALPHA8_EXT
                                                 ? VROColorRenderingMode::Linear
                                                 : VROColorRenderingMode::NonLinear);
    }

    // Create one swapchain per eye
    for (uint32_t eye = 0; eye < 2; ++eye) {
        auto &view  = viewConfigs[eye];
        auto &sc    = _swapchains[eye];

        sc.width  = view.recommendedImageRectWidth;
        sc.height = view.recommendedImageRectHeight;

        XrSwapchainCreateInfo scInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        scInfo.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                              XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        scInfo.format      = chosenFormat;
        scInfo.sampleCount = view.recommendedSwapchainSampleCount;
        scInfo.width       = sc.width;
        scInfo.height      = sc.height;
        scInfo.faceCount   = 1;
        scInfo.arraySize   = 1;
        scInfo.mipCount    = 1;

        XR_RETURN_FALSE(xrCreateSwapchain(_session, &scInfo, &sc.handle));

        // Enumerate swapchain images (GL textures)
        uint32_t imgCount = 0;
        xrEnumerateSwapchainImages(sc.handle, 0, &imgCount, nullptr);
        sc.images.resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
        xrEnumerateSwapchainImages(sc.handle, imgCount, &imgCount,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(sc.images.data()));

        ALOGV("Eye %u: swapchain %ux%u  %u images", eye, sc.width, sc.height, imgCount);
    }

    // Initialise fixed foveation once swapchains exist. Non-fatal on failure:
    // a runtime without FB_foveation simply renders unfoveated.
    if (_foveationAvailable && _swapchainUpdateStateAvailable) {
        if (initFoveation()) {
            // Default to a sensible level for high-PPD HMDs; app can override.
            setFoveationLevel(VROFoveationLevel::MEDIUM, /*dynamic=*/true);
        }
    }
    return true;
}

// ── Foveation (XR_FB_foveation) ───────────────────────────────────────────────
bool VROSceneRendererOpenXR::initFoveation() {
    auto loadFn = [&](const char *name, void **fn) -> bool {
        XrResult r = xrGetInstanceProcAddr(_instance, name, (PFN_xrVoidFunction *)fn);
        if (XR_FAILED(r)) { ALOGW("xrGetInstanceProcAddr('%s') failed: %d", name, (int)r); return false; }
        return (*fn != nullptr);
    };
    bool ok = true;
    ok &= loadFn("xrCreateFoveationProfileFB",  (void **)&_pfnCreateFoveationProfile);
    ok &= loadFn("xrDestroyFoveationProfileFB", (void **)&_pfnDestroyFoveationProfile);
    ok &= loadFn("xrUpdateSwapchainFB",         (void **)&_pfnUpdateSwapchain);
    if (!ok) {
        ALOGW("XR_FB_foveation functions unavailable — foveation disabled");
        _foveationAvailable = false;
        return false;
    }
    ALOGI("Foveation available (eyeTracked=%d)", (int)_eyeTrackedFoveationAvailable);
    return true;
}

bool VROSceneRendererOpenXR::setFoveationLevel(VROFoveationLevel level, bool dynamic) {
    if (!_foveationAvailable || _pfnCreateFoveationProfile == nullptr) return false;

    XrFoveationLevelFB xrLevel;
    switch (level) {
        case VROFoveationLevel::OFF:    xrLevel = XR_FOVEATION_LEVEL_NONE_FB;   break;
        case VROFoveationLevel::LOW:    xrLevel = XR_FOVEATION_LEVEL_LOW_FB;    break;
        case VROFoveationLevel::MEDIUM: xrLevel = XR_FOVEATION_LEVEL_MEDIUM_FB; break;
        case VROFoveationLevel::HIGH:   xrLevel = XR_FOVEATION_LEVEL_HIGH_FB;   break;
        default:                        xrLevel = XR_FOVEATION_LEVEL_MEDIUM_FB; break;
    }

    XrFoveationLevelProfileCreateInfoFB levelInfo = {
        XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB
    };
    levelInfo.level          = xrLevel;
    levelInfo.verticalOffset = 0.0f;
    levelInfo.dynamic        = dynamic ? XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB
                                       : XR_FOVEATION_DYNAMIC_DISABLED_FB;

    XrFoveationProfileCreateInfoFB profileInfo = {
        XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB
    };
    profileInfo.next = &levelInfo;

    bool allApplied = true;
    for (uint32_t eye = 0; eye < 2; ++eye) {
        XrFoveationProfileFB profile = XR_NULL_HANDLE;
        XrResult r = _pfnCreateFoveationProfile(_session, &profileInfo, &profile);
        if (XR_FAILED(r)) { ALOGW("xrCreateFoveationProfileFB failed (eye %u): %d", eye, (int)r); allApplied = false; continue; }

        XrSwapchainStateFoveationFB state = { XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB };
        state.profile = profile;
        r = _pfnUpdateSwapchain(_swapchains[eye].handle,
                                reinterpret_cast<XrSwapchainStateBaseHeaderFB *>(&state));
        if (XR_FAILED(r)) { ALOGW("xrUpdateSwapchainFB failed (eye %u): %d", eye, (int)r); allApplied = false; }

        _pfnDestroyFoveationProfile(profile);
    }
    ALOGI("Foveation set: level=%d dynamic=%d applied=%d", (int)level, (int)dynamic, (int)allApplied);
    return allApplied;
}

// ── Passthrough (XR_FB_passthrough) ───────────────────────────────────────────
bool VROSceneRendererOpenXR::initPassthrough() {
    // Extension functions are NOT direct API calls — they must be loaded via
    // xrGetInstanceProcAddr. The extension guard (XR_FB_passthrough) was already
    // checked during instance creation; if we reach here it was enabled.

    auto loadFn = [&](const char *name, void **fn) -> bool {
        XrResult r = xrGetInstanceProcAddr(_instance, name, (PFN_xrVoidFunction *)fn);
        if (XR_FAILED(r)) {
            ALOGW("xrGetInstanceProcAddr('%s') failed: %d", name, (int)r);
            return false;
        }
        return (*fn != nullptr);
    };

    bool ok = true;
    ok &= loadFn("xrCreatePassthroughFB",       (void **)&_pfnCreatePassthrough);
    ok &= loadFn("xrDestroyPassthroughFB",      (void **)&_pfnDestroyPassthrough);
    ok &= loadFn("xrPassthroughStartFB",        (void **)&_pfnPassthroughStart);
    ok &= loadFn("xrPassthroughPauseFB",        (void **)&_pfnPassthroughPause);
    ok &= loadFn("xrCreatePassthroughLayerFB",  (void **)&_pfnCreatePassthroughLayer);
    ok &= loadFn("xrDestroyPassthroughLayerFB", (void **)&_pfnDestroyPassthroughLayer);
    ok &= loadFn("xrPassthroughLayerResumeFB",  (void **)&_pfnPassthroughLayerResume);
    ok &= loadFn("xrPassthroughLayerPauseFB",   (void **)&_pfnPassthroughLayerPause);
    // Style is optional — don't fail init if it's missing (older runtimes).
    loadFn("xrPassthroughLayerSetStyleFB",      (void **)&_pfnPassthroughLayerSetStyle);

    if (!ok) {
        ALOGW("XR_FB_passthrough functions not fully available — passthrough disabled");
        return false;
    }

    // Create the XrPassthroughFB handle. Do NOT set the running-at-creation flag
    // so passthrough starts in paused state — enabled only on demand.
    XrPassthroughCreateInfoFB ptInfo = { XR_TYPE_PASSTHROUGH_CREATE_INFO_FB };
    ptInfo.flags = 0;
    XrResult r = _pfnCreatePassthrough(_session, &ptInfo, &_passthrough);
    if (XR_FAILED(r)) {
        ALOGE("xrCreatePassthroughFB failed: %d", (int)r);
        return false;
    }

    // Create a full-reconstruction layer. The layer itself starts running
    // (XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB) but we immediately pause it;
    // this avoids a stop/start round-trip on first enable.
    XrPassthroughLayerCreateInfoFB layerInfo = { XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB };
    layerInfo.passthrough = _passthrough;
    layerInfo.flags       = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    layerInfo.purpose     = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    r = _pfnCreatePassthroughLayer(_session, &layerInfo, &_passthroughLayer);
    if (XR_FAILED(r)) {
        ALOGE("xrCreatePassthroughLayerFB failed: %d", (int)r);
        _pfnDestroyPassthrough(_passthrough);
        _passthrough = XR_NULL_HANDLE;
        return false;
    }

    // Pause immediately — layer is ready but not composited until setPassthroughEnabled(true).
    _pfnPassthroughLayerPause(_passthroughLayer);

    ALOGV("XR_FB_passthrough initialised (paused — call setPassthroughEnabled(true) to enable)");
    return true;
}

void VROSceneRendererOpenXR::setPassthroughEnabled(bool enabled) {
    if (_passthrough == XR_NULL_HANDLE || _passthroughLayer == XR_NULL_HANDLE) {
        ALOGW("setPassthroughEnabled(%s): XR_FB_passthrough not available on this device",
              enabled ? "true" : "false");
        _passthroughEnabled = false;
        return;
    }

    if (enabled) {
        // Ensure the passthrough subsystem is running before resuming the layer.
        XR_CHECK(_pfnPassthroughStart(_passthrough));
        XR_CHECK(_pfnPassthroughLayerResume(_passthroughLayer));
    } else {
        // Pause the layer first, then pause the subsystem (saves power).
        XR_CHECK(_pfnPassthroughLayerPause(_passthroughLayer));
        XR_CHECK(_pfnPassthroughPause(_passthrough));
    }

    _passthroughEnabled = enabled;

    // The OpenXR display clears the swapchain opaque (alpha 1) for VR; for
    // passthrough it must clear TRANSPARENT (alpha 0) so empty regions reveal the
    // passthrough layer beneath. This is the authoritative per-frame clear (the
    // base render pass binds the display every frame), so flip it here.
    if (_openxrDriver) {
        auto display = _openxrDriver->getOpenXRDisplay();
        if (display) display->setClearAlpha(enabled ? 0.0f : 1.0f);
    }

    ALOGV("setPassthroughEnabled: %s", enabled ? "true" : "false");
}

void VROSceneRendererOpenXR::setPassthroughStyle(float opacity, float edgeR, float edgeG,
                                                 float edgeB, float edgeA) {
    if (_passthroughLayer == XR_NULL_HANDLE || _pfnPassthroughLayerSetStyle == nullptr) {
        ALOGW("setPassthroughStyle: passthrough layer styling not available");
        return;
    }

    XrPassthroughStyleFB style = { XR_TYPE_PASSTHROUGH_STYLE_FB };
    style.textureOpacityFactor = opacity;          // [0,1]
    style.edgeColor            = { edgeR, edgeG, edgeB, edgeA };  // alpha 0 = no edge

    XrResult r = _pfnPassthroughLayerSetStyle(_passthroughLayer, &style);
    if (XR_FAILED(r)) {
        ALOGW("xrPassthroughLayerSetStyleFB failed: %d", (int)r);
        return;
    }
    ALOGV("setPassthroughStyle: opacity=%.2f edge=(%.2f,%.2f,%.2f,%.2f)",
          opacity, edgeR, edgeG, edgeB, edgeA);
}

void VROSceneRendererOpenXR::setHandTrackingEnabled(bool enabled) {
    if (_inputController) {
        _inputController->setHandTrackingEnabled(enabled);
    }
}

void VROSceneRendererOpenXR::setSceneController(
        std::shared_ptr<VROSceneController> sceneController) {
    VROSceneRenderer::setSceneController(sceneController);
    attachARSceneIfNeeded(sceneController);
}

void VROSceneRendererOpenXR::setSceneController(
        std::shared_ptr<VROSceneController> sceneController, float seconds,
        VROTimingFunctionType timingFunction) {
    // ViroViewOpenXR drives scene changes through this timed variant.
    VROSceneRenderer::setSceneController(sceneController, seconds, timingFunction);
    attachARSceneIfNeeded(sceneController);
}

void VROSceneRendererOpenXR::attachARSceneIfNeeded(
        std::shared_ptr<VROSceneController> sceneController) {
    // A plain VR scene (VROScene) needs no AR-specific wiring.
    if (!sceneController) {
        ALOGV("attachAR: null sceneController");
        return;
    }
    std::shared_ptr<VROScene> scene = sceneController->getScene();
    std::shared_ptr<VROARScene> arScene = std::dynamic_pointer_cast<VROARScene>(scene);
    ALOGV("attachAR: sceneController set, scene=%p isARScene=%d arSession=%d",
          (void *)scene.get(), (int)(arScene != nullptr), (int)(_arSession != nullptr));
    if (!arScene) {
        return;  // plain VR scene
    }

    // An AR scene on Quest is mixed reality — turn on passthrough so the room is
    // visible behind virtual content. This is independent of plane detection:
    // even when XR_EXT_plane_detection is unavailable, the AR scene still renders
    // over passthrough. (The passthroughEnabled prop can also drive this.)
    setPassthroughEnabled(true);

    // CRITICAL for passthrough to show through: the projection layer is composited
    // over passthrough with ALPHA_BLEND, so empty areas must have alpha 0. The
    // default clear colour is opaque black {0,0,0,1}, which would hide passthrough
    // entirely. Clear transparent so only rendered geometry occludes the room.
    if (_renderer) {
        _renderer->setClearColor({ 0, 0, 0, 0 }, _driver);
    }

    arScene->setDriver(_driver);

    // Wire the OpenXR AR session to the scene's anchor delegate so onAnchorFound /
    // anchorUpdated / anchorRemoved (and ViroARPlane) fire — mirrors
    // VROSceneRendererARCore. Only possible when plane detection is available.
    if (_arSession) {
        arScene->setARSession(_arSession);
        _arSession->setScene(arScene);
        _arSession->setDelegate(arScene->getSessionDelegate());
        _arSession->run();
        ALOGV("AR scene attached to OpenXR — passthrough + plane detection active");
    } else {
        ALOGV("AR scene attached to OpenXR — passthrough active "
              "(XR_EXT_plane_detection unavailable; no plane anchors)");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Teardown
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::destroySwapchains() {
    for (auto &sc : _swapchains) {
        if (sc.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(sc.handle);
            sc.handle = XR_NULL_HANDLE;
        }
        sc.images.clear();
    }
}

void VROSceneRendererOpenXR::destroySession() {
    // AR session teardown — destroy the plane detector before the XrSession.
    if (_arSession) {
        _arSession->destroyPlaneDetector();
        _arSession.reset();
    }

    // Passthrough teardown — must use loaded function pointers, not direct calls.
    if (_passthroughLayer != XR_NULL_HANDLE && _pfnDestroyPassthroughLayer) {
        _pfnDestroyPassthroughLayer(_passthroughLayer);
        _passthroughLayer = XR_NULL_HANDLE;
    }
    if (_passthrough != XR_NULL_HANDLE && _pfnDestroyPassthrough) {
        _pfnDestroyPassthrough(_passthrough);
        _passthrough = XR_NULL_HANDLE;
    }
    destroySwapchains();
    if (_appSpace != XR_NULL_HANDLE) {
        xrDestroySpace(_appSpace);
        _appSpace = XR_NULL_HANDLE;
    }
    if (_session != XR_NULL_HANDLE) {
        xrDestroySession(_session);
        _session = XR_NULL_HANDLE;
    }
}

void VROSceneRendererOpenXR::destroyEGLContext() {
    if (_eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (_eglSurface  != EGL_NO_SURFACE) eglDestroySurface(_eglDisplay, _eglSurface);
        if (_eglContext  != EGL_NO_CONTEXT) eglDestroyContext(_eglDisplay, _eglContext);
        eglTerminate(_eglDisplay);
    }
    _eglDisplay = EGL_NO_DISPLAY;
    _eglContext = EGL_NO_CONTEXT;
    _eglSurface = EGL_NO_SURFACE;
}

void VROSceneRendererOpenXR::destroyOpenXR() {
    if (_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(_instance);
        _instance = XR_NULL_HANDLE;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// VROSceneRenderer lifecycle
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::onStart() {
    ALOGV("onStart");
}

void VROSceneRendererOpenXR::onResume() {
    // Idempotent: must be safe to call multiple times. The Java side may end up
    // calling this from both onAttachedToWindow (manual catch-up for a missed
    // onResume) AND a later RN LifecycleEventListener.onHostResume firing.
    // std::thread::operator= terminates the process if the LHS is joinable, so
    // assigning a new thread on top of an already-running render thread aborts
    // the app. Re-entering when already running just clears the paused flag.
    if (_running) {
        ALOGV("onResume — already running, clearing paused flag");
        _paused = false;
        return;
    }
    ALOGV("onResume — starting render thread");
    _paused  = false;
    _running = true;
    _renderThread = std::thread(&VROSceneRendererOpenXR::renderLoop, this);
}

void VROSceneRendererOpenXR::onPause() {
    if (!_running || _paused) {
        ALOGV("onPause — already paused or not running, skipping");
        return;
    }
    ALOGV("onPause — pausing render thread");
    _paused = true;
    // The render loop checks _paused and idles; session state will transition
    // to VISIBLE or IDLE via xrPollEvent naturally.
}

void VROSceneRendererOpenXR::onStop() {
    ALOGV("onStop — stopping render thread");
    _running = false;
    if (_renderThread.joinable()) {
        _renderThread.join();
    }
}

void VROSceneRendererOpenXR::onDestroy() {
    onStop();
    if (_inputController) {
        _inputController->destroyHandTrackers();
        _inputController->destroySpaces();
    }
    destroySession();
    destroyEGLContext();
    destroyOpenXR();

    // Release global refs after the render thread is fully stopped.
    //
    // onDestroy is invoked from `Java_com_viro_core_Renderer_nativeDestroyRenderer`,
    // i.e. the JVM main thread already attached by the JNI bridge. Calling
    // DetachCurrentThread on a thread that's mid-JNI-call aborts ART with
    // "attempting to detach while still running code" (SIGABRT). Use GetEnv
    // to detect attached state and only Attach/Detach if we were actually
    // running on a non-attached thread.
    if (_jvm && _jview) {
        JNIEnv *env       = nullptr;
        bool    attached  = false;
        jint    res       = _jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            } else {
                env = nullptr;
            }
        }
        if (env) {
            env->DeleteGlobalRef(_jview);
            env->DeleteGlobalRef(_activity);
        }
        if (attached) {
            _jvm->DetachCurrentThread();
        }
        _jview    = nullptr;
        _activity = nullptr;
    }
    ALOGV("VROSceneRendererOpenXR destroyed");
}

void VROSceneRendererOpenXR::onTouchEvent(int /*action*/, float /*x*/, float /*y*/) {
    // Quest has no touchscreen — controller events handled in input controller
}

void VROSceneRendererOpenXR::onKeyEvent(int /*keyCode*/, int /*action*/) {}

void VROSceneRendererOpenXR::recenterTracking() {
    if (_session == XR_NULL_HANDLE) return;

    // Create a temporary VIEW space to locate the head pose.
    XrReferenceSpaceCreateInfo viewSpaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    viewSpaceInfo.referenceSpaceType    = XR_REFERENCE_SPACE_TYPE_VIEW;
    viewSpaceInfo.poseInReferenceSpace  = { {0, 0, 0, 1}, {0, 0, 0} };
    XrSpace viewSpace = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateReferenceSpace(_session, &viewSpaceInfo, &viewSpace))) {
        ALOGE("recenterTracking: failed to create VIEW space");
        return;
    }

    // Locate head (VIEW) relative to the current app space.
    // Use _lastPredictedDisplayTime so the pose is from the most recent frame.
    XrSpaceLocation headLoc = { XR_TYPE_SPACE_LOCATION };
    xrLocateSpace(viewSpace, _appSpace, _lastPredictedDisplayTime, &headLoc);
    xrDestroySpace(viewSpace);

    if (!(headLoc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        ALOGW("recenterTracking: head orientation not valid — skipped");
        return;
    }

    // Extract yaw from head orientation (keep scene upright — Y-axis rotation only).
    XrQuaternionf &q = headLoc.pose.orientation;
    float yaw = atan2f(2.0f * (q.w * q.y + q.x * q.z),
                       1.0f - 2.0f * (q.y * q.y + q.z * q.z));

    // Rebuild the same origin type at the current head XZ and yaw. The vertical
    // origin is preserved per mode: eye-level keeps y=0; native LOCAL_FLOOR
    // keeps the floor at y=0; the emulated floor re-derives the STAGE offset now
    // (the user may have moved to a different floor height).
    XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType = _appSpaceType;
    float originY = 0.0f;
    if (_appSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL &&
        _trackingOrigin == VROTrackingOrigin::Floor) {
        float offsetY = 0.0f;
        if (deriveFloorOffset(_lastPredictedDisplayTime, &offsetY)) {
            _floorOffsetY = offsetY;
        }
        originY = _floorOffsetY;  // keep the last good offset if this probe missed
    }
    spaceInfo.poseInReferenceSpace.position    = { headLoc.pose.position.x,
                                                    originY,
                                                    headLoc.pose.position.z };
    spaceInfo.poseInReferenceSpace.orientation = { 0.0f,
                                                    sinf(yaw * 0.5f),
                                                    0.0f,
                                                    cosf(yaw * 0.5f) };

    XrSpace newSpace = XR_NULL_HANDLE;
    if (XR_SUCCEEDED(xrCreateReferenceSpace(_session, &spaceInfo, &newSpace))) {
        xrDestroySpace(_appSpace);
        _appSpace = newSpace;
        ALOGV("recenterTracking: recentered (yaw=%.2f rad, originY=%.3f)", yaw, originY);
    } else {
        ALOGE("recenterTracking: xrCreateReferenceSpace failed");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Render thread
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::renderLoop() {
    prctl(PR_SET_NAME, "VROOpenXRRender", 0, 0, 0);
    ALOGV("Render thread started");

    // Make the EGL context current on this thread (main thread released it in constructor)
    EGLBoolean eglOk = eglMakeCurrent(_eglDisplay, _eglSurface, _eglSurface, _eglContext);
    if (!eglOk) {
        ALOGE("eglMakeCurrent failed on render thread: 0x%x", eglGetError());
        return;
    }
    ALOGV("EGL context current on render thread");

    // Mark this thread as the Viro renderer thread so passert_thread() checks pass.
    VROThreadRestricted::setThread(VROThreadName::Renderer);

    // Attach to JVM so we can call back into Java each frame (drains mRenderQueue /
    // FrameListeners registered by the user from the React Native side).
    JNIEnv *env = nullptr;
    _jvm->AttachCurrentThread(&env, nullptr);
    jclass   viewCls       = env->GetObjectClass(_jview);
    jmethodID onDrawFrameM = env->GetMethodID(viewCls, "onDrawFrame", "()V");
    env->DeleteLocalRef(viewCls);

    while (_running) {
        pollEvents();

        if (_paused || _sessionState == XR_SESSION_STATE_IDLE ||
                       _sessionState == XR_SESSION_STATE_UNKNOWN) {
            usleep(100000);  // 100ms — don't burn CPU when idle/paused
            continue;
        }

        // OpenXR spec: the frame loop (xrWaitFrame/xrBeginFrame/xrEndFrame) must run
        // as soon as the session is running (_sessionRunning=true, set in READY handler
        // when xrBeginSession succeeds). The runtime only transitions from READY →
        // SYNCHRONIZED after it sees the first completed frame loop iteration.
        if (_sessionRunning) {
            renderFrame();

            // Drive Java FrameListeners / PlatformUtil render-thread callbacks
            // only when the app has visible content (VISIBLE or FOCUSED states).
            if (_sessionState == XR_SESSION_STATE_VISIBLE ||
                _sessionState == XR_SESSION_STATE_FOCUSED) {
                env->CallVoidMethod(_jview, onDrawFrameM);
            }
        }
    }

    _jvm->DetachCurrentThread();
    eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    ALOGV("Render thread exited");
}

void VROSceneRendererOpenXR::pollEvents() {
    XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(_instance, &event) == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto *stateEvent =
                    reinterpret_cast<XrEventDataSessionStateChanged *>(&event);
                handleSessionStateChange(stateEvent);
                break;
            }
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
                // The runtime is recentering our reference-space type (e.g. the
                // user long-pressed Home on PICO / held the Oculus button). The
                // pose we located content against is about to move, so rebuild
                // the app space to match — otherwise the world stays pinned to
                // the pre-recenter origin and drifts off from the user.
                auto *changeEvent =
                    reinterpret_cast<XrEventDataReferenceSpaceChangePending *>(&event);
                if (changeEvent->referenceSpaceType == _appSpaceType &&
                    _session != XR_NULL_HANDLE) {
                    XrSpace rebuilt = XR_NULL_HANDLE;
                    XrReferenceSpaceType rebuiltType = _appSpaceType;
                    if (buildReferenceSpace(_trackingOrigin, &rebuilt, &rebuiltType)) {
                        XrSpace old = _appSpace;
                        _appSpace     = rebuilt;
                        _appSpaceType = rebuiltType;
                        if (old != XR_NULL_HANDLE) xrDestroySpace(old);
                        ALOGI("Reference space rebuilt after runtime recenter");
                    }
                } else {
                    ALOGV("Reference space change pending (type %d) — ignored",
                          (int)changeEvent->referenceSpaceType);
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                ALOGE("Instance loss pending — shutting down");
                _running = false;
                break;
            case XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB:
            case XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB:
            case XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB:
                // XR_FB_scene room-entity query + component-enable events.
                if (_arSession) {
                    _arSession->onSpatialEvent(event);
                }
                break;
            default:
                break;
        }
        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

void VROSceneRendererOpenXR::handleSessionStateChange(
        XrEventDataSessionStateChanged *event) {

    _sessionState = event->state;
    ALOGV("Session state → %d", (int)_sessionState);

    switch (_sessionState) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
            beginInfo.primaryViewConfigurationType =
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XR_CHECK(xrBeginSession(_session, &beginInfo));
            _sessionRunning = true;
            ALOGV("Session began");
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            XR_CHECK(xrEndSession(_session));
            _sessionRunning = false;
            ALOGV("Session ended");
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
        case XR_SESSION_STATE_EXITING:
            _running = false;
            break;
        default:
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Frame render
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::renderFrame() {
    // Drain pending renderer tasks (setSceneController, texture uploads, etc.)
    // submitted via VROPlatformDispatchAsyncRenderer from any thread.
    VROPlatformDrainRendererQueue();

    // ── Wait for the display ──────────────────────────────────────────────────
    XrFrameWaitInfo waitInfo  = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState    frameState= { XR_TYPE_FRAME_STATE };
    XR_CHECK(xrWaitFrame(_session, &waitInfo, &frameState));
    _lastPredictedDisplayTime = frameState.predictedDisplayTime;

    // ── Begin frame ───────────────────────────────────────────────────────────
    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    XR_CHECK(xrBeginFrame(_session, &beginInfo));

    // Locate eye views
    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime            = frameState.predictedDisplayTime;
    locateInfo.space                  = _appSpace;

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t    viewCount = 2;
    XrView      views[2]  = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    XR_CHECK(xrLocateViews(_session, &locateInfo, &viewState, 2, &viewCount, views));

    // Drive the Quest MR (AR) session: query XR_EXT_plane_detection and fan
    // detected planes through the VROARScene anchor pipeline. Runs before
    // prepareFrame() so anchor node transforms are current for this frame.
    if (_arSession) {
        _arSession->setDisplayTime(frameState.predictedDisplayTime);
        _arSession->updateFrame();
    }

    // Render each eye if views are valid
    std::vector<XrCompositionLayerProjectionView> projViews(2);

    bool viewsValid =
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

    if (viewsValid && frameState.shouldRender) {
        // ── Prepare the Viro renderer (once per frame, before any eye render) ─
        // Use the left eye pose/fov to drive scene-level preparation (culling,
        // animation ticks, physics, etc.). Per-eye projection is applied in renderEye.
        if (_renderer && _renderer->hasRenderContext()) {
            VROViewport leftViewport(0, 0, _swapchains[0].width, _swapchains[0].height);

            // Convert OpenXR half-angle tangents (radians, signed) → degrees, positive
            constexpr float kRad2Deg = 180.0f / M_PI;
            const XrFovf &fov0 = views[0].fov;
            VROFieldOfView viroFov(
                -fov0.angleLeft  * kRad2Deg,   // left  half-angle (positive)
                 fov0.angleRight * kRad2Deg,   // right half-angle
                -fov0.angleDown  * kRad2Deg,   // bottom half-angle
                 fov0.angleUp    * kRad2Deg    // top half-angle
            );
            // headRotation must be rotation-only. VROMatrix4f::multiply(VROVector3f) always
            // adds the matrix's translation column (m[12..14]), so passing the full pose
            // matrix shifts the computed camera forward/up vectors by the head's stage-space
            // position (~1.6 m on Y), tilting the frustum upward and culling objects placed
            // at (0, 0, -2). Strip translation so only orientation drives the frustum.
            // Per-eye view matrices (which include head position) are computed in renderEye().
            VROMatrix4f headPoseMatrix  = xrPoseToMatrix(views[0].pose);
            headPoseMatrix[12] = 0.0f;
            headPoseMatrix[13] = 0.0f;
            headPoseMatrix[14] = 0.0f;
            VROMatrix4f leftProjMatrix  = xrFovToProjection(fov0);

            _renderer->prepareFrame(_frame++, leftViewport, viroFov,
                                    headPoseMatrix, leftProjMatrix, _driver);
        }

        // Process input after prepareFrame so the camera is valid for hit-testing.
        if (_inputController && _renderer && _renderer->hasRenderContext()) {
            _inputController->onProcess(_session, _appSpace,
                                         frameState.predictedDisplayTime,
                                         _renderer->getCamera());
        }

        for (uint32_t eye = 0; eye < 2; ++eye) {
            renderEye((int)eye, views[eye], _swapchains[eye]);

            projViews[eye]                  = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projViews[eye].pose             = views[eye].pose;
            projViews[eye].fov              = views[eye].fov;
            projViews[eye].subImage.swapchain              = _swapchains[eye].handle;
            projViews[eye].subImage.imageRect.offset       = { 0, 0 };
            projViews[eye].subImage.imageRect.extent       = {
                (int32_t)_swapchains[eye].width,
                (int32_t)_swapchains[eye].height
            };
            projViews[eye].subImage.imageArrayIndex = 0;
        }

        // Tick the frame scheduler so that queued tasks (texture hydration,
        // model load callbacks, etc.) are processed. Must be called after
        // prepareFrame() and after all eye rendering is done.
        if (_renderer && _renderer->hasRenderContext()) {
            _renderer->endFrame(_driver);
        }
    }

    // ── End frame — assemble layer list ──────────────────────────────────────
    std::vector<const XrCompositionLayerBaseHeader *> layers;

    // Passthrough layer (behind projection)
    XrCompositionLayerPassthroughFB ptLayerComp = {
        XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB
    };
    if (_passthroughEnabled && _passthroughLayer != XR_NULL_HANDLE) {
        ptLayerComp.layerHandle = _passthroughLayer;
        ptLayerComp.flags       = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&ptLayerComp));
    }

    // Projection layer (3D scene)
    XrCompositionLayerProjection projLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    if (viewsValid && frameState.shouldRender) {
        projLayer.space      = _appSpace;
        projLayer.viewCount  = 2;
        projLayer.views      = projViews.data();
        // When passthrough is on, the scene is rendered with a transparent clear
        // (alpha 0). Tell the compositor to honor the layer's alpha channel so
        // passthrough shows through empty regions; otherwise the layer is treated
        // as opaque and hides the room.
        if (_passthroughEnabled) {
            projLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                   XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        }
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projLayer));
    }

    XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime          = frameState.predictedDisplayTime;
    // Passthrough is supplied as a composition layer (underlay) beneath the
    // projection layer, so the environment blend mode stays OPAQUE. The projection
    // layer's SOURCE_ALPHA bit + the display's transparent clear (alpha 0 in empty
    // regions) let the passthrough layer show through where there's no geometry.
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount  = (uint32_t)layers.size();
    // OpenXR spec: layers must be NULL when layerCount==0
    endInfo.layers      = layers.empty() ? nullptr : layers.data();

    static uint32_t sFrameLog = 0;
    if (sFrameLog++ < 10 || (sFrameLog % 90 == 0)) {
        ALOGV("xrEndFrame: state=%d shouldRender=%d viewsValid=%d layerCount=%u",
              (int)_sessionState, (int)frameState.shouldRender,
              (int)viewsValid, endInfo.layerCount);
    }

    XrResult endResult = xrEndFrame(_session, &endInfo);
    if (XR_FAILED(endResult)) {
        ALOGE("xrEndFrame FAILED: result=%d state=%d shouldRender=%d layerCount=%u",
              (int)endResult, (int)_sessionState,
              (int)frameState.shouldRender, endInfo.layerCount);
    }
}

void VROSceneRendererOpenXR::renderEye(int eyeIndex,
                                        const XrView &view,
                                        VROOpenXRSwapchain &swapchain) {
    // ── Acquire swapchain image ───────────────────────────────────────────────
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    uint32_t imageIndex = 0;
    XR_CHECK(xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &imageIndex));

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    XR_CHECK(xrWaitSwapchainImage(swapchain.handle, &waitInfo));

    // ── Bind the FBO for this eye ─────────────────────────────────────────────
    GLuint colorTex = swapchain.images[imageIndex].image;

    auto display = _openxrDriver->getOpenXRDisplay();
    display->setSwapchainImage(colorTex,
                                (GLsizei)swapchain.width,
                                (GLsizei)swapchain.height);
    VROViewport viewport(0, 0, swapchain.width, swapchain.height);
    display->setViewport(viewport);
    display->bind();

    // For passthrough (MR), explicitly clear the swapchain to TRANSPARENT. Viro's
    // base render pass (VROPortalTreeRenderPass) never clears the colour buffer —
    // it assumes an opaque background fills the view (a skybox, or the camera quad
    // on ARCore). An AR scene on Quest has neither, so without this the swapchain
    // keeps stale opaque content and the projection layer hides passthrough. We
    // clear here, after bind, so the subsequent scene geometry draws on top and
    // empty regions stay alpha 0 — letting the passthrough layer beneath show.
    // (The transparent clear for passthrough is done inside display->bind() above,
    // via setClearAlpha(0) — that's the authoritative per-frame clear.)

    // ── Compute view + projection matrices ───────────────────────────────────
    // viewMatrix = inverted pose (world→eye transform for rendering)
    VROMatrix4f viewMatrix = xrPoseToMatrix(view.pose).invert();
    VROMatrix4f projMatrix = xrFovToProjection(view.fov);

    // ── Render the Viro scene for this eye ───────────────────────────────────
    if (_renderer && _renderer->hasRenderContext()) {
        VROEyeType eyeType = (eyeIndex == 0) ? VROEyeType::Left : VROEyeType::Right;
        _renderer->renderEye(eyeType, viewMatrix, projMatrix, viewport, _driver);
    } else {
        // Renderer not yet initialized — clear to black so the swapchain is valid.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // ── Release swapchain image ───────────────────────────────────────────────
    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XR_CHECK(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo));
}
