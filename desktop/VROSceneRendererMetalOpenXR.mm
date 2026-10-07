//
//  VROSceneRendererMetalOpenXR.mm
//
//  Desktop (macOS) OpenXR scene renderer bound through XR_KHR_metal_enable.
//  Mirrors VROSceneRendererOpenXR (android/sharedCode/src/main/cpp) frame
//  semantics — xrWaitFrame → xrBeginFrame → xrLocateViews → per-eye
//  acquire/wait/render/release → XrCompositionLayerProjection → xrEndFrame —
//  minus JNI/EGL/Android.
//
//  Projection view pose/FOV are always copied from xrLocateViews output for
//  the frame's predictedDisplayTime: Meta XR Simulator silently discards
//  projection views with fabricated poses (accepted layer, black output).
//
//  SOT-KEYWORDS: openxr, metal, macos, simulator, scene-renderer
//

#include "VROSceneRendererMetalOpenXR.h"

#define XR_USE_GRAPHICS_API_METAL 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "VROPassthroughXR.h"
#include "VROSimSceneContent.h"

#import <Metal/Metal.h>
#import <AppKit/AppKit.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <vector>

// When compiled with -DVIRO_DESKTOP_SCENE, the per-eye render draws real Viro
// scene content. By default the REAL VRORenderer drives the frame through the
// Metal substrate path (VRODriverMetalOpenXR); -DVRO_BRIDGE_FALLBACK keeps the
// old hand-rolled bridge pipeline for A/B comparison.
#if VIRO_DESKTOP_SCENE
#include "VROSceneGraphMetalBridge.h"
#if !VRO_BRIDGE_FALLBACK
#include "VRORenderer.h"
#include "VRORendererConfiguration.h"
#include "VROSceneController.h"
#include "VROScene.h"
#include "VRONode.h"
#include "VROBox.h"
#include "VROMaterial.h"
#include "VROMaterialVisual.h"
#include "VROMatrix4f.h"
#include "VROVector3f.h"
#include "VROVector4f.h"
#include "VROQuaternion.h"
#include "VROEye.h"
#include "VROViewport.h"
#include "VROFieldOfView.h"
#include "VRODriverMetalOpenXR.h"
#include "VROInputControllerXR.h"
#endif
#endif

namespace {

#define XR_CHECK(expr)                                                         \
    do {                                                                       \
        XrResult r_ = (expr);                                                  \
        if (XR_FAILED(r_)) {                                                   \
            std::fprintf(stderr, "%s failed: %d\n", #expr, (int)r_);           \
            return false;                                                      \
        }                                                                      \
    } while (0)

// Hand tracking, gaze and pinch live in VROInputControllerXR — see
// desktop/VROInputControllerXR.mm. The session/renderer code here only feeds
// it session state + predicted display time.

// ---- Minimal Metal draw ------------------------------------------------------
// One vertex format for both render modes: interleaved position+color, plus a
// single MVP uniform block. The fallback triangle passes an identity matrix;
// the Viro path passes proj*view*model computed with VROMatrix4f.

struct Vertex {
    float pos[3];
    float col[3];
};

struct Uniforms {
    float mvp[16]; // column-major float4x4
};

const char *kShaderSrc = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Vertex { float3 pos; float3 col; };
struct Uniforms { float4x4 mvp; };
struct VOut { float4 pos [[position]]; float3 col; };
vertex VOut vmain(uint vid [[vertex_id]],
                  constant Vertex* verts [[buffer(0)]],
                  constant Uniforms& u [[buffer(1)]]) {
    VOut o;
    o.pos = u.mvp * float4(verts[vid].pos, 1.0);
    o.col = verts[vid].col;
    return o;
}
fragment float4 fmain(VOut in [[stage_in]]) {
    return float4(in.col, 1.0);
}
)MSL";

// Identity matrix, column-major.
const Uniforms kIdentityUniforms = {{
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
}};

#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
// Identical to VROSceneRendererOpenXR.cpp::xrPoseToMatrix.
VROMatrix4f xrPoseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f rot = q.getMatrix();
    rot[12] = pose.position.x;
    rot[13] = pose.position.y;
    rot[14] = pose.position.z;
    return rot;
}

// VROSceneRendererOpenXR.cpp::xrFovToProjection with the z row remapped to
// Metal's [0,1] NDC depth (the GL-style [-1,1] variant clips the front half
// of clip space on Metal).
VROMatrix4f xrFovToProjectionMetal(const XrFovf &fov,
                                   float nearZ = 0.1f,
                                   float farZ  = 100.0f) {
    const float left   = tanf(fov.angleLeft);
    const float right  = tanf(fov.angleRight);
    const float down   = tanf(fov.angleDown);
    const float up     = tanf(fov.angleUp);

    const float w  =  right - left;
    const float h  =  up    - down;
    const float q  =  farZ / (nearZ - farZ);
    const float qn =  farZ * nearZ / (nearZ - farZ);

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

// Scene-side of the input milestone: a Viro event delegate on the box node
// that toggles the material diffuse color blue <-> orange on ClickDown
// (a pinch landed while the pointer ray hit the box). Hover transitions are
// logged so the gaze hit-test path is observable in the run log.
class VROSimBoxDelegate : public VROEventDelegate {
public:
    VROSimBoxDelegate(std::shared_ptr<VROMaterial> material) :
        _material(material) {
        setEnabledEvent(VROEventDelegate::EventAction::OnClick, true);
        setEnabledEvent(VROEventDelegate::EventAction::OnHover, true);
    }
    void onClick(int source, std::shared_ptr<VRONode> node,
                 ClickState clickState, std::vector<float> position) override {
        if (clickState == ClickState::ClickDown) {
            _orange = !_orange;
            _material->getDiffuse().setColor(
                _orange ? VROVector4f(1.0f, 0.55f, 0.05f, 1.0f)
                        : VROVector4f(0.15f, 0.65f, 1.0f, 1.0f));
            std::printf("input-xr: BOX CLICK -> %s\n",
                        _orange ? "ORANGE" : "BLUE");
        }
    }
    void onHover(int source, std::shared_ptr<VRONode> node, bool isHovering,
                 std::vector<float> position) override {
        std::printf("input-xr: box hover %s\n", isHovering ? "ENTER" : "EXIT");
    }
private:
    std::shared_ptr<VROMaterial> _material;
    bool _orange = false;
};
#endif

} // namespace

struct VROSceneRendererMetalOpenXR::Impl {

    // OpenXR handles
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace stageSpace = XR_NULL_HANDLE;
    XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    // VIRO_PASSTHROUGH=1 asks for Passthrough; if the system can't provide
    // it, the mode becomes PassthroughFallback (immersive over slate).
    enum class DisplayMode { Immersive, Passthrough, PassthroughFallback };
    DisplayMode displayMode = DisplayMode::Immersive;
    bool passthroughExt = false;
    std::unique_ptr<VROPassthroughXR> passthrough;

    struct EyeSwapchain {
        XrSwapchain handle = XR_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<XrSwapchainImageMetalKHR> images;
    };
    std::vector<EyeSwapchain> eyes;
    std::vector<XrViewConfigurationView> viewConfigs;

    // Metal objects (retained through __bridge cast into void* members is
    // unsafe under ARC-free .mm; keep as ObjC ivars — this TU is ObjC++)
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLDepthStencilState> depthState = nil;
    id<MTLTexture> depthTexture[8] = {};
    id<MTLBuffer> vertexBuffer = nil;
    id<MTLBuffer> indexBuffer = nil;
    id<MTLBuffer> uniformBuffers[2] = {nil, nil};
    uint32_t indexCount = 0;
    bool viroScene = false;

    int frameBudget = 600;

#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
    // Real VRORenderer path: driver + renderer + a one-node scene.
    std::shared_ptr<VRODriverMetalOpenXR> _driver;
    std::shared_ptr<VRORenderer> _renderer;
    std::shared_ptr<VROInputControllerXR> _inputController;
    std::shared_ptr<VROSceneController> _sceneController;
    std::shared_ptr<VRONode> _boxNode;
    // VRONode holds its event delegate weakly; this keeps it alive.
    std::shared_ptr<VROEventDelegate> _boxDelegate;

    bool buildViroRendererScene() {
        _driver = std::make_shared<VRODriverMetalOpenXR>(device, queue);
        _driver->initialize();
        _inputController = std::make_shared<VROInputControllerXR>(_driver);

        // All optional passes off — the driver reports a GPU without MRT, so
        // the choreographer renders the scene directly to the display target.
        VRORendererConfiguration config;
        config.enableShadows = false;
        config.enableBloom = false;
        config.enableHDR = false;
        config.enablePBR = false;
        _renderer = std::make_shared<VRORenderer>(config, _inputController);

        _sceneController = std::make_shared<VROSceneController>();
        std::shared_ptr<VROScene> scene = _sceneController->getScene();

        std::shared_ptr<VROMaterial> material = std::make_shared<VROMaterial>();
        // Lit and textured, so captures cover texture sampling and lighting.
        material->setLightingModel(VROLightingModel::Lambert);
        material->getDiffuse().setTexture(VROSimMakeCheckerTexture(256, 8));
        material->getDiffuse().setColor(VROVector4f(1, 1, 1, 1));

        std::shared_ptr<VROBox> box = VROBox::createBox(0.5f, 0.5f, 0.5f);
        box->setMaterials({ material });

        _boxNode = std::make_shared<VRONode>();
        _boxNode->setGeometry(box);
        _boxNode->setPosition(VROVector3f(0, 0, -1.5f));
        // Click (pinch-while-gazing) toggles diffuse color — the visible
        // scene response for the input milestone.
        _boxDelegate = std::make_shared<VROSimBoxDelegate>(material);
        _boxNode->setEventDelegate(_boxDelegate);
        scene->getRootNode()->addChildNode(_boxNode);
        VROSimAddLights(scene->getRootNode());

        _renderer->setSceneController(_sceneController, _driver);
        // Passthrough clears to transparent so the camera feed shows through.
        // A requested-but-unavailable passthrough clears to slate instead of
        // black, so the fallback is visible and not mistaken for a dead frame.
        _renderer->setClearColor(
            displayMode == DisplayMode::Passthrough ? VROVector4f(0, 0, 0, 0)
            : displayMode == DisplayMode::PassthroughFallback ? VROVector4f(0.16f, 0.18f, 0.24f, 1)
                                                              : VROVector4f(0, 0, 0, 1),
            _driver);

        // Attach happens while the session is still IDLE (spec requirement
        // for xrAttachSessionActionSets): action set + eye-gaze binding +
        // pinch bindings + gaze action space + hand trackers.
        _inputController->bindXR(instance, session, stageSpace);

        // Per-eye depth targets (the swapchain supplies color only).
        for (uint32_t v = 0; v < eyes.size() && v < 8; ++v) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                width:eyes[v].width height:eyes[v].height mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget;
            td.storageMode = MTLStorageModePrivate;
            depthTexture[v] = [device newTextureWithDescriptor:td];
        }
        std::puts("viro renderer scene built (real VRORenderer, Metal substrate)");
        return true;
    }

    // prepareFrame bookkeeping once per frame, before the per-eye renders.
    void prepareViroFrame() {
        VROViewport viewport(0, 0, (int)eyes[0].width, (int)eyes[0].height);
        const float kRad2Deg = 180.0f / (float)M_PI;
        const XrFovf &f0 = _lastFov[0];
        VROFieldOfView fov(-f0.angleLeft  * kRad2Deg,
                            f0.angleRight * kRad2Deg,
                           -f0.angleDown  * kRad2Deg,
                            f0.angleUp    * kRad2Deg);
        // Rotation-only head pose for camera/frustum (see android notes).
        VROMatrix4f headPose = xrPoseToMatrix(_lastPose[0]);
        headPose[12] = headPose[13] = headPose[14] = 0.0f;
        VROMatrix4f proj = xrFovToProjectionMetal(f0);
        if (_boxNode) {
            _boxNode->setRotation(VROQuaternion::fromAngleAxis(_frame * 0.01f,
                                                             VROVector3f(0, 1, 0)));
        }
        _renderer->prepareFrame(_frame, viewport, fov, headPose, proj, _driver);
    }
#endif

    // ---- init: instance → system → Metal binding → session → swapchains ----
    bool init() {
        const char *wanted[] = {
            XR_KHR_METAL_ENABLE_EXTENSION_NAME,
            "XR_EXT_user_presence",
            XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME,
            XR_EXT_HAND_INTERACTION_EXTENSION_NAME,
            XR_EXT_HAND_TRACKING_EXTENSION_NAME,
        };
        uint32_t extCount = 0;
        XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr));
        std::vector<XrExtensionProperties> avail(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
        XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, avail.data()));
        const char *env = getenv("VIRO_PASSTHROUGH");
        const bool wantPassthrough = env && std::strcmp(env, "1") == 0;
        if (wantPassthrough) displayMode = DisplayMode::Passthrough;
        // VIRO_XR_DISABLE_EXT hides extensions from this app, to exercise the
        // missing-capability paths on a runtime that has everything.
        // Comma-separated, exact names: XR_FB_passthrough must not hide
        // XR_FB_passthrough_keyboard_hands or the reverse.
        std::vector<std::string> disabled;
        if (const char *d = getenv("VIRO_XR_DISABLE_EXT")) {
            std::string list(d);
            for (size_t start = 0; start <= list.size();) {
                size_t end = list.find(',', start);
                if (end == std::string::npos) end = list.size();
                if (end > start) disabled.push_back(list.substr(start, end - start));
                start = end + 1;
            }
        }
        std::vector<const char *> want(std::begin(wanted), std::end(wanted));
        if (wantPassthrough) want.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
        std::vector<const char *> enabled;
        for (const char *w : want) {
            bool found = false;
            for (const auto &p : avail)
                if (std::strcmp(p.extensionName, w) == 0) found = true;
            if (found && std::find(disabled.begin(), disabled.end(), w) != disabled.end()) {
                std::printf("ext %s: hidden by VIRO_XR_DISABLE_EXT\n", w);
                found = false;
            } else {
                std::printf("ext %s: %s\n", w, found ? "yes" : "NO");
            }
            if (found) enabled.push_back(w);
            if (found && std::strcmp(w, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0)
                passthroughExt = true;
        }

        XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        std::strcpy(createInfo.applicationInfo.applicationName, "viro-sim-host");
        createInfo.applicationInfo.applicationVersion = 1;
        std::strcpy(createInfo.applicationInfo.engineName, "viro-desktop-metal");
        createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 1, 0);
        createInfo.enabledExtensionCount = (uint32_t)enabled.size();
        createInfo.enabledExtensionNames = enabled.data();
        XR_CHECK(xrCreateInstance(&createInfo, &instance));

        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
        XR_CHECK(xrGetInstanceProperties(instance, &props));
        std::printf("runtime: %s %u.%u.%u\n", props.runtimeName,
                    XR_VERSION_MAJOR(props.runtimeVersion),
                    XR_VERSION_MINOR(props.runtimeVersion),
                    XR_VERSION_PATCH(props.runtimeVersion));
        std::printf("openxr api: %u.%u.%u  host: %s  backend: Metal (XR_KHR_metal_enable)\n",
                    XR_VERSION_MAJOR(XR_CURRENT_API_VERSION),
                    XR_VERSION_MINOR(XR_CURRENT_API_VERSION),
                    XR_VERSION_PATCH(XR_CURRENT_API_VERSION),
                    [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String]);

        XrSystemGetInfo sysInfo{XR_TYPE_SYSTEM_GET_INFO};
        sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XR_CHECK(xrGetSystem(instance, &sysInfo, &system));
        XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
        XR_CHECK(xrGetSystemProperties(instance, system, &sysProps));
        std::printf("system: %s\n", sysProps.systemName);

        // Metal graphics requirements + binding (XR_KHR_metal_enable).
        PFN_xrGetMetalGraphicsRequirementsKHR pfnGetMetalReqs = nullptr;
        XR_CHECK(xrGetInstanceProcAddr(instance, "xrGetMetalGraphicsRequirementsKHR",
                                       (PFN_xrVoidFunction *)&pfnGetMetalReqs));
        device = MTLCreateSystemDefaultDevice();
        if (!device) { std::fputs("no Metal device\n", stderr); return false; }
        XrGraphicsRequirementsMetalKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_METAL_KHR};
        XR_CHECK(pfnGetMetalReqs(instance, system, &reqs));
        id<MTLDevice> required = (__bridge id<MTLDevice>)reqs.metalDevice;
        std::printf("metal reqs: device=%p ours=%p same=%d\n",
                    (void *)reqs.metalDevice, (void *)device,
                    required == device);
        if (required && required != device) {
            std::puts("using runtime-required Metal device");
            device = required;
        }

        queue = [device newCommandQueue];
        XrGraphicsBindingMetalKHR binding{XR_TYPE_GRAPHICS_BINDING_METAL_KHR};
        binding.commandQueue = (__bridge void *)queue;

        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.systemId = system;
        sessionInfo.next = &binding;
        XR_CHECK(xrCreateSession(instance, &sessionInfo, &session));
        std::puts("session created (Metal-bound)");

        // Blend modes: immersive wants OPAQUE, passthrough wants ALPHA_BLEND.
        // If the wanted mode is missing, take the runtime's first and say so.
        uint32_t bmCount = 0;
        XR_CHECK(xrEnumerateEnvironmentBlendModes(instance, system,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &bmCount, nullptr));
        std::vector<XrEnvironmentBlendMode> blendModes(bmCount);
        XR_CHECK(xrEnumerateEnvironmentBlendModes(instance, system,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, bmCount, &bmCount, blendModes.data()));
        auto hasMode = [&](XrEnvironmentBlendMode m) {
            return std::find(blendModes.begin(), blendModes.end(), m) != blendModes.end();
        };
        std::printf("blend modes:");
        for (auto b : blendModes) std::printf(" %d", (int)b);
        std::putchar('\n');
        if (displayMode == DisplayMode::Passthrough) {
            auto started = VROPassthroughXR::start(instance, session, passthroughExt,
                hasMode(XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND));
            if (auto *pt = std::get_if<std::unique_ptr<VROPassthroughXR>>(&started)) {
                passthrough = std::move(*pt);
                std::puts("passthrough layer running");
            } else {
                std::printf("passthrough unavailable: %s; falling back to immersive\n",
                            std::get<std::string>(started).c_str());
                displayMode = DisplayMode::PassthroughFallback;
            }
        }
        XrEnvironmentBlendMode wantedMode = displayMode == DisplayMode::Passthrough
            ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        if (hasMode(wantedMode) || blendModes.empty()) {
            blendMode = wantedMode;
        } else {
            blendMode = blendModes[0];
            std::printf("blend mode %d not offered by this system\n", (int)wantedMode);
        }
        std::printf("mode: %s, blend %d\n",
                    displayMode == DisplayMode::Passthrough ? "passthrough"
                    : displayMode == DisplayMode::PassthroughFallback
                        ? "immersive (passthrough fallback)" : "immersive",
                    (int)blendMode);

        uint32_t viewCount = 0;
        XR_CHECK(xrEnumerateViewConfigurationViews(instance, system,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr));
        viewConfigs.assign(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        XR_CHECK(xrEnumerateViewConfigurationViews(instance, system,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, viewConfigs.data()));

        uint32_t fmtCount = 0;
        XR_CHECK(xrEnumerateSwapchainFormats(session, 0, &fmtCount, nullptr));
        std::vector<int64_t> formats(fmtCount);
        XR_CHECK(xrEnumerateSwapchainFormats(session, fmtCount, &fmtCount, formats.data()));
        std::printf("swapchain formats:");
        for (int64_t f : formats) std::printf(" %lld", (long long)f);
        std::putchar('\n');

        // One swapchain per eye, mirroring VROSceneRendererOpenXR.
        eyes.resize(viewCount);
        for (uint32_t v = 0; v < viewCount; ++v) {
            XrSwapchainCreateInfo scInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            scInfo.format = formats.empty() ? 80 /* MTLPixelFormatBGRA8Unorm */ : formats[0];
            scInfo.width = viewConfigs[v].recommendedImageRectWidth;
            scInfo.height = viewConfigs[v].recommendedImageRectHeight;
            scInfo.arraySize = 1;
            scInfo.mipCount = 1;
            scInfo.faceCount = 1;
            scInfo.sampleCount = viewConfigs[v].recommendedSwapchainSampleCount;
            scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            XR_CHECK(xrCreateSwapchain(session, &scInfo, &eyes[v].handle));
            eyes[v].width = scInfo.width;
            eyes[v].height = scInfo.height;
            uint32_t imgCount = 0;
            XR_CHECK(xrEnumerateSwapchainImages(eyes[v].handle, 0, &imgCount, nullptr));
            eyes[v].images.assign(imgCount, {XR_TYPE_SWAPCHAIN_IMAGE_METAL_KHR});
            XR_CHECK(xrEnumerateSwapchainImages(eyes[v].handle, imgCount, &imgCount,
                (XrSwapchainImageBaseHeader *)eyes[v].images.data()));
            std::printf("eye %u swapchain: %ux%u x%u images, format %lld (of %u views)\n",
                        v, scInfo.width, scInfo.height, imgCount,
                        (long long)scInfo.format, viewCount);
        }

        XrReferenceSpaceCreateInfo refSpace{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        refSpace.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        refSpace.poseInReferenceSpace.orientation = {0, 0, 0, 1};
        XR_CHECK(xrCreateReferenceSpace(session, &refSpace, &stageSpace));

#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
        return buildViroRendererScene();
#else
        return buildRenderResources();
#endif
    }

    // MSL pipeline + geometry. Under VIRO_DESKTOP_SCENE the vertex/index data
    // comes from VROBox (real Viro geometry); otherwise a fixed triangle.
    bool buildRenderResources() {
        NSError *err = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:
            [NSString stringWithUTF8String:kShaderSrc] options:nil error:&err];
        if (!lib) {
            std::fprintf(stderr, "shader compile failed: %s\n",
                         err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
#if VIRO_DESKTOP_SCENE
        // Real Viro path: VROBox → VROGeometry sources/elements → GPU buffers,
        // per-eye MVP from VROMatrix4f math driven by xrLocateViews output.
        viroScene = VROBuildBoxGeometry(device, &vertexBuffer, &indexBuffer,
                                      &indexCount);
        if (!viroScene) {
            std::fputs("Viro box build failed, using triangle\n", stderr);
        }
#endif
        if (!viroScene) {
            // Fallback: centered triangle, near-white gradient colors, NDC space.
            Vertex tri[3] = {
                {{-0.8f, -0.8f, 0.f}, {1.f, 0.f, 1.f}},
                {{ 0.8f, -0.8f, 0.f}, {1.f, 1.f, 0.3f}},
                {{ 0.0f,  0.8f, 0.f}, {0.3f, 1.f, 0.7f}},
            };
            vertexBuffer = [device newBufferWithBytes:tri length:sizeof(tri)
                                              options:MTLResourceStorageModeShared];
        }

        MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = [lib newFunctionWithName:@"vmain"];
        pd.fragmentFunction = [lib newFunctionWithName:@"fmain"];
        pd.colorAttachments[0].pixelFormat =
            (__bridge id<MTLTexture>)eyes[0].images[0].texture
                ? [(__bridge id<MTLTexture>)eyes[0].images[0].texture pixelFormat]
                : MTLPixelFormatBGRA8Unorm;
        // Depth only when the Viro path is active — the fallback render pass
        // attaches no depth texture, and a pipeline/pass format mismatch
        // invalidates the encoder.
        if (viroScene) {
            pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        }
        pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!pipeline) {
            std::fprintf(stderr, "pipeline failed: %s\n",
                         err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
        if (viroScene) {
            // Depth: no depth test for the NDC-space fallback triangle;
            // depth-test the Viro box (its projection is a real perspective
            // matrix remapped to Metal's [0,1] NDC z).
            MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
            dd.depthCompareFunction = MTLCompareFunctionLess;
            dd.depthWriteEnabled = YES;
            depthState = [device newDepthStencilStateWithDescriptor:dd];
            for (uint32_t v = 0; v < eyes.size() && v < 8; ++v) {
                MTLTextureDescriptor *td = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                    width:eyes[v].width height:eyes[v].height mipmapped:NO];
                td.usage = MTLTextureUsageRenderTarget;
                td.storageMode = MTLStorageModePrivate;
                depthTexture[v] = [device newTextureWithDescriptor:td];
            }
        }

        for (int e = 0; e < 2 && e < (int)eyes.size(); ++e)
            uniformBuffers[e] = [device newBufferWithLength:sizeof(Uniforms)
                                                    options:MTLResourceStorageModeShared];
        return true;
    }

    // Per-eye render into the acquired swapchain texture. When `readback` is
    // non-nil, the swapchain texture is blit-copied into it after the render
    // pass (a second render attachment would need a separate pipeline — the
    // primary pipeline only declares color attachment 0).
    void renderEye(uint32_t eye, id<MTLTexture> tex, id<MTLTexture> readback) {
        Uniforms u = kIdentityUniforms;
#if VIRO_DESKTOP_SCENE
        if (viroScene) {
            // _lastPose/_lastFov were filled by xrLocateViews this frame.
            VROComputeEyeUniforms(eye, _lastPose[eye], _lastFov[eye], _frame, u.mvp);
        }
#endif
        memcpy(uniformBuffers[eye].contents, &u, sizeof(u));

        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = tex;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        // Distinct clear colors per eye keep Gate C able to attribute output.
        rp.colorAttachments[0].clearColor =
            (eye == 0) ? MTLClearColorMake(1.0, 0.0, 1.0, 1.0)
                       : MTLClearColorMake(0.0, 0.6, 1.0, 1.0);
        if (viroScene && depthTexture[eye]) {
            rp.depthAttachment.texture = depthTexture[eye];
            rp.depthAttachment.loadAction = MTLLoadActionClear;
            rp.depthAttachment.storeAction = MTLStoreActionDontCare;
            rp.depthAttachment.clearDepth = 1.0;
        }
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:pipeline];
        if (viroScene) [enc setDepthStencilState:depthState];
        [enc setVertexBuffer:vertexBuffer offset:0 atIndex:0];
        [enc setVertexBuffer:uniformBuffers[eye] offset:0 atIndex:1];
        if (indexBuffer && indexCount) {
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:indexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:indexBuffer
                     indexBufferOffset:0];
        } else {
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
        [enc endEncoding];
        if (readback) {
            id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
            [blit copyFromTexture:tex toTexture:readback];
            [blit endEncoding];
        }
        [cmd commit];
        [cmd waitUntilCompleted];
        static bool loggedStatus = false;
        if (!loggedStatus) {
            loggedStatus = true;
            std::printf("mtl cmd status=%ld error=%s\n", (long)cmd.status,
                        cmd.error ? cmd.error.localizedDescription.UTF8String : "none");
        }
    }

#if VIRO_DESKTOP_SCENE
    XrPosef _lastPose[8];
    XrFovf _lastFov[8];
#endif
    int _frame = 0;

    // ---- frame loop (mirrors VROSceneRendererOpenXR::renderFrame) -----------
    int run() {
        if (!init()) {
            teardown();
            return 1;
        }
        if (const char *fb = getenv("VIRO_FRAMES")) frameBudget = atoi(fb);
        std::printf("frame budget: %d\n", frameBudget);

        const uint32_t viewCount = (uint32_t)eyes.size();
        std::vector<XrCompositionLayerProjectionView> pvViews(viewCount,
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
        for (uint32_t v = 0; v < viewCount; ++v) {
            pvViews[v].subImage.swapchain = eyes[v].handle;
            pvViews[v].subImage.imageRect =
                {{0, 0}, {(int32_t)eyes[v].width, (int32_t)eyes[v].height}};
            pvViews[v].subImage.imageArrayIndex = 0;
        }
        XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        proj.space = stageSpace;
        proj.viewCount = viewCount;
        proj.views = pvViews.data();
        std::vector<const XrCompositionLayerBaseHeader *> layers;
        if (passthrough) {
            layers.push_back(passthrough->layer());
            // The scene's transparent clear must reveal the layer underneath.
            proj.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        }
        layers.push_back((const XrCompositionLayerBaseHeader *)&proj);
        std::vector<XrView> locatedViews(viewCount, {XR_TYPE_VIEW});

        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

        bool running = false, done = false;
        int frames = 0;
        for (int i = 0; i < 100000 && !done; ++i) {
            XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
            while (xrPollEvent(instance, &ev) == XR_SUCCESS) {
                if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    auto *sc = (XrEventDataSessionStateChanged *)&ev;
                    std::printf("session state -> %d\n", (int)sc->state);
#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
                    // The input controller gates xrSyncActions on FOCUSED.
                    if (_inputController) _inputController->setSessionState(sc->state);
#endif
                    if (sc->state == XR_SESSION_STATE_READY && !running) {
                        xrBeginSession(session, &beginInfo);
                        running = true;
                    } else if (sc->state == XR_SESSION_STATE_EXITING ||
                               sc->state == XR_SESSION_STATE_LOSS_PENDING) {
                        i = 100000;
                    }
                }
                ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
            }
            @autoreleasepool {
                NSEvent *nsev = nil;
                while ((nsev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                  untilDate:[NSDate distantPast]
                                     inMode:NSDefaultRunLoopMode dequeue:YES])) {
                    [NSApp sendEvent:nsev];
                }
                [NSApp updateWindows];
            }
            if (!running) { usleep(1000); continue; }

            XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState frameState{XR_TYPE_FRAME_STATE};
            if (XR_FAILED(xrWaitFrame(session, &waitInfo, &frameState))) {
                usleep(1000); continue;
            }
            XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
            xrBeginFrame(session, &bi);
            XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
            ei.displayTime = frameState.predictedDisplayTime;
            ei.environmentBlendMode = blendMode;

            if (frameState.shouldRender) {
                // Populate projection views from xrLocateViews for THIS frame's
                // predictedDisplayTime — fabricated poses are silently dropped
                // by this runtime (accepted layer, black composited output).
                XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
                vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                vli.displayTime = frameState.predictedDisplayTime;
                vli.space = stageSpace;
                XrViewState vs{XR_TYPE_VIEW_STATE};
                uint32_t located = viewCount;
                bool viewsValid = false;
                if (XR_SUCCEEDED(xrLocateViews(session, &vli, &vs, viewCount,
                                               &located, locatedViews.data()))) {
                    viewsValid =
                        (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
                        (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
                    for (uint32_t v = 0; v < located && v < viewCount; ++v) {
                        pvViews[v].pose = locatedViews[v].pose;
                        pvViews[v].fov = locatedViews[v].fov;
                        static bool fovLogged[8] = {};
                        if (v < 8 && !fovLogged[v]) {
                            fovLogged[v] = true;
                            const XrFovf &f = locatedViews[v].fov;
                            float spanX = tanf(f.angleRight) - tanf(f.angleLeft);
                            float spanY = tanf(f.angleUp) - tanf(f.angleDown);
                            // On a correct swapchain the tangent-span ratio
                            // equals the pixel ratio, so pixels are square.
                            std::printf("eye %u fov L%.1f R%.1f U%.1f D%.1f deg; "
                                        "tan span %.3fx%.3f (%.3f) vs swapchain %ux%u (%.3f)\n",
                                        v, f.angleLeft * 57.2958f, f.angleRight * 57.2958f,
                                        f.angleUp * 57.2958f, f.angleDown * 57.2958f,
                                        spanX, spanY, spanX / spanY, eyes[v].width,
                                        eyes[v].height, (float)eyes[v].width / eyes[v].height);
                        }
#if VIRO_DESKTOP_SCENE
                        _lastPose[v] = locatedViews[v].pose;
                        _lastFov[v] = locatedViews[v].fov;
#endif
                    }
                }
#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
                // Hand the frame's predicted display time to the input
                // controller; prepareFrame() -> onProcess(camera) then uses
                // it for xrSyncActions/xrLocateSpace/xrLocateHandJointsEXT.
                _inputController->setFrameTime(frameState.predictedDisplayTime);
                if (viewsValid) {
                    prepareViroFrame();
                }
#endif
                for (uint32_t v = 0; v < viewCount && viewsValid; ++v) {
                    uint32_t imgIndex = 0;
                    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    if (XR_FAILED(xrAcquireSwapchainImage(eyes[v].handle, &ai, &imgIndex)))
                        continue;
                    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    wi.timeout = 1000000000;
                    if (XR_SUCCEEDED(xrWaitSwapchainImage(eyes[v].handle, &wi))) {
                        id<MTLTexture> tex =
                            (__bridge id<MTLTexture>)eyes[v].images[imgIndex].texture;
                        static bool readbackDone = false;
                        id<MTLTexture> copy = nil;
                        if (!readbackDone) {
                            // Gate A readback: swapchain textures may be private
                            // storage, so render to a second shared attachment.
                            MTLTextureDescriptor *rd = [MTLTextureDescriptor
                                texture2DDescriptorWithPixelFormat:tex.pixelFormat
                                width:tex.width height:tex.height mipmapped:NO];
                            rd.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
                            rd.storageMode = MTLStorageModeShared;
                            copy = [device newTextureWithDescriptor:rd];
                        }
#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
                        _driver->beginEye(tex, depthTexture[v]);
                        {
                            VROMatrix4f viewM = xrPoseToMatrix(_lastPose[v]).invert();
                            VROMatrix4f projM = xrFovToProjectionMetal(_lastFov[v]);
                            VROViewport eyeVp(0, 0, (int)tex.width, (int)tex.height);
                            _renderer->renderEye(v == 0 ? VROEyeType::Left
                                                        : VROEyeType::Right,
                                                 viewM, projM, eyeVp, _driver);
                        }
                        _driver->endEye(copy);
#else
                        renderEye(v, tex, copy);
#endif
                        if (copy) {
                            readbackDone = true;
                            size_t bpr = tex.width * 4;
                            std::vector<uint8_t> px(bpr * tex.height);
                            [copy getBytes:px.data() bytesPerRow:bpr
                                fromRegion:MTLRegionMake2D(0, 0, tex.width, tex.height)
                                mipmapLevel:0];
                            size_t off = (tex.height / 2) * bpr + (tex.width / 2) * 4;
                            // Asymmetric FOVs (Quest 3) put content off-center,
                            // so count lit pixels across the whole image too.
                            size_t lit = 0;
                            for (size_t i = 0; i < px.size(); i += 4) {
                                if (px[i] > 20 || px[i + 1] > 20 || px[i + 2] > 20) ++lit;
                            }
                            std::printf("eye %u readback center pixel = %02x %02x %02x %02x lit=%zu/%zu\n",
                                        v, px[off], px[off + 1], px[off + 2], px[off + 3],
                                        lit, px.size() / 4);
                        }
                    }
                    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    xrReleaseSwapchainImage(eyes[v].handle, &ri);
                }

                if (viewsValid) {
#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
                    _renderer->endFrame(_driver);
#endif
                    ei.layerCount = (uint32_t)layers.size();
                    ei.layers = layers.data();
                    ++_frame;
                    if (++frames >= frameBudget) done = true;
                }
            }
            XrResult er = xrEndFrame(session, &ei);
            if (XR_FAILED(er)) {
                static int erLogs = 0;
                if (++erLogs <= 5) std::printf("xrEndFrame failed: %d\n", (int)er);
            }
            usleep(1000);
        }

        if (!done) {
            std::fputs("session never reached RUNNING\n", stderr);
            teardown();
            return 1;
        }
        std::printf("rendered %d frames, requesting exit\n", frames);

        // Clean teardown: request exit, drain to EXITING, then destroy.
        xrRequestExitSession(session);
        for (int i = 0; i < 200; ++i) {
            XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
            bool ended = false;
            while (xrPollEvent(instance, &ev) == XR_SUCCESS) {
                if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    auto *sc = (XrEventDataSessionStateChanged *)&ev;
                    std::printf("session state -> %d\n", (int)sc->state);
                    if (sc->state == XR_SESSION_STATE_STOPPING) xrEndSession(session);
                    if (sc->state == XR_SESSION_STATE_EXITING) ended = true;
                }
                ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
            }
            if (ended) break;
            usleep(4000);
        }
        teardown();
        std::puts("viro_sim_host ok");
        return 0;
    }

    void teardown() {
#if VIRO_DESKTOP_SCENE && !VRO_BRIDGE_FALLBACK
        // Input controller owns hand trackers, the gaze action space and the
        // action set — all session-scoped, so shut down before xrDestroySession.
        if (_inputController) _inputController->shutdownXR();
#endif
        passthrough.reset();  // session-scoped: before xrDestroySession
        for (auto &e : eyes)
            if (e.handle != XR_NULL_HANDLE) xrDestroySwapchain(e.handle);
        eyes.clear();
        if (stageSpace != XR_NULL_HANDLE) xrDestroySpace(stageSpace);
        if (session != XR_NULL_HANDLE) xrDestroySession(session);
        if (instance != XR_NULL_HANDLE) xrDestroyInstance(instance);
    }
};

VROSceneRendererMetalOpenXR::VROSceneRendererMetalOpenXR()
    : _impl(new Impl()) {}
VROSceneRendererMetalOpenXR::~VROSceneRendererMetalOpenXR() = default;

int VROSceneRendererMetalOpenXR::run() {
    return _impl->run();
}
