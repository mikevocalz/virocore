# viro_sim_host — minimal macOS OpenXR host that drives Viro-rendered frames
# through Meta XR Simulator via XR_KHR_metal_enable.
#
# The OpenXR loader + headers come from the Khronos SDK (the repo's vendored
# headers under android/ are an Android-era drop without XR_KHR_metal_enable).
# If offline, pass -DFETCHCONTENT_SOURCE_DIR_OPENXR=<path to an openxr source
# checkout>, e.g. an existing ~/tools/xr-probe/build/_deps/openxr-src.
#
# SOT-KEYWORDS: cmake, openxr, metal, macos, simulator
cmake_minimum_required(VERSION 3.24)
project(viro_sim_host CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
  openxr
  GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK-Source.git
  GIT_TAG release-1.1.41
  GIT_SHALLOW TRUE
)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(openxr)

# VIRO_DESKTOP_SCENE: render a Viro scene through the real VRORenderer and the
# shared Metal substrate. OFF builds the triangle-only minimal host.
option(VIRO_DESKTOP_SCENE "Render a Viro scene via VRORenderer + Metal" ON)
# VRO_BRIDGE_FALLBACK: use the hand-rolled bridge pipeline
# (VROSceneGraphMetalBridge) instead of VRORenderer.
option(VRO_BRIDGE_FALLBACK "Use bridge renderer instead of real VRORenderer" OFF)

set(VIRO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(VIRO_RENDERER_DIR ${VIRO_ROOT}/ViroRenderer)
set(VIRO_METAL_DIR ${VIRO_ROOT}/ios/ViroKit/VisionOS)

add_executable(viro_sim_host
  main.mm
  VROSceneRendererMetalOpenXR.mm
  VROPassthroughXR.mm
  VROSimSceneContent.mm
)

if(VIRO_DESKTOP_SCENE)
  # freetype backs VRODriverVisionOS's typeface factory. Homebrew ships it for
  # macOS; the ios/Libraries copies are iOS and xros only.
  find_package(Freetype REQUIRED)

  target_compile_definitions(viro_sim_host PRIVATE
    VIRO_DESKTOP_SCENE=1
    VRO_METAL=1
    VRO_BRIDGE_FALLBACK=$<BOOL:${VRO_BRIDGE_FALLBACK}>)
  target_include_directories(viro_sim_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${VIRO_RENDERER_DIR}
    ${VIRO_RENDERER_DIR}/poly2tri
    ${VIRO_METAL_DIR}
    # macos/ViroKit before ios/ViroKit: both carry VROViewScene.h and only the
    # macOS one builds without UIKit. ios/ViroKit supplies VROTypefaceiOS and
    # VROVideoTextureCacheMetal.
    ${VIRO_ROOT}/macos/ViroKit
    ${VIRO_ROOT}/ios/ViroKit
    ${VIRO_ROOT}/wasm/libs/bullet/include
    ${VIRO_ROOT}/wasm/libs/bullet/src
    ${FREETYPE_INCLUDE_DIRS})
  target_sources(viro_sim_host PRIVATE
    VROSceneGraphMetalBridge.mm
    VRODriverMetalOpenXR.mm
    VROInputControllerXR.mm)

  # Renderer sources: the same selection ios/add_visionos_target.rb makes for
  # the ViroKitVisionOS target, so the desktop host runs the Metal stack that
  # ships on visionOS. That target's driver, render targets, post-process and
  # Metal render passes live in ios/ViroKit/VisionOS and are platform-neutral
  # Metal; only their file location says visionOS.
  file(GLOB VIRO_RENDERER_CPP CONFIGURE_DEPENDS ${VIRO_RENDERER_DIR}/*.cpp)
  set(VIRO_EXCLUDE_REGEX
    "OpenGL|FBX|Nodes\\.pb|gzip_stream|cnpy|Body(Mesh|Track|Surface|Player|Anim|IK)|AnimBody|SkeletonRenderer|VROIKRig|VROIKTest|VROPose|VROOneEuro"
    "|AR(Session|Frame|Camera|Hit|Image|Object|Declarative|Imperative|Scene|Anchor|Constrain|Depth|WorldMesh|Shadow|Plane|Draggable)|VROInputControllerAR|VROARImperative|VROARNode"
    "|GVR|Cardboard|Distortion|Stereoscopic|VROPhysicsTest|VROHeadTracker|TestHarness|RendererTest|Test\\.cpp$|VROTestUtil|VROTextTest|VRODebugHUD|VROReticle"
    "|VROBRDFRenderPass|VROToneMappingRenderPass|VROGaussianBlurRenderPass|VROIrradianceRenderPass|VROPrefilterRenderPass|VROEquirectangularToCubeRenderPass|VROIBLPreprocess"
    "|VROPortal|VROObjectRecogniz"
    "|VROShaderProgram\\.cpp$|VRORenderUtil\\.cpp$|VROShaderFactory\\.cpp$|VROImageShaderProgram\\.cpp$|VROBoneUBO\\.cpp$|VROLightingUBO\\.cpp$|VROParticleUBO\\.cpp$|VROInstancedUBO\\.cpp$")
  string(JOIN "" VIRO_EXCLUDE_REGEX ${VIRO_EXCLUDE_REGEX})
  list(FILTER VIRO_RENDERER_CPP EXCLUDE REGEX "${VIRO_EXCLUDE_REGEX}")
  file(GLOB_RECURSE VIRO_POLY2TRI CONFIGURE_DEPENDS ${VIRO_RENDERER_DIR}/poly2tri/*.cc)
  # The visionOS target lists the engine extension backends explicitly.
  file(GLOB VIRO_EXTENSION_CPP CONFIGURE_DEPENDS ${VIRO_RENDERER_DIR}/extension/*.cpp)

  set(VIRO_SOURCES
    ${VIRO_RENDERER_CPP}
    ${VIRO_POLY2TRI}
    ${VIRO_EXTENSION_CPP}
    ${VIRO_RENDERER_DIR}/tiny_obj_loader.cc
    # The visionOS target leaves the reticle out; VRORenderer::renderHUD still
    # calls it, and this host has a controller pointer that can show one.
    ${VIRO_RENDERER_DIR}/VROReticle.cpp
    ${VIRO_ROOT}/ios/ViroKit/VROExternalSurfaceTextureImpl.mm
    ${VIRO_METAL_DIR}/VRODriverVisionOS.mm
    ${VIRO_METAL_DIR}/VRORenderTargetMetal.mm
    ${VIRO_METAL_DIR}/VROMetalPostProcess.mm
    ${VIRO_METAL_DIR}/VROMetalFrameTimer.mm
    ${VIRO_METAL_DIR}/VROVisionOSRenderStubs.cpp
    ${VIRO_ROOT}/ios/ViroKit/VROVideoTextureCacheMetal.cpp
    ${VIRO_ROOT}/ios/ViroKit/VROTypefaceiOS.cpp
    ${VIRO_ROOT}/macos/ViroKit/VROImageMacOS.cpp
    ${VIRO_ROOT}/macos/ViroKit/VROConvert.cpp)
  # Every .cpp builds as Objective-C++: VRODefines.h only selects
  # VRO_PLATFORM_MACOS under __OBJC__, and the Metal headers need it.
  set_source_files_properties(${VIRO_SOURCES} PROPERTIES
    COMPILE_FLAGS "-x objective-c++ -fno-objc-arc")
  target_sources(viro_sim_host PRIVATE ${VIRO_SOURCES})

  # Bullet physics, built from the vendored sources (the macos/Libraries
  # binaries are x86_64 only). VRONode and VROScene link the physics classes
  # whether or not a scene uses them.
  file(GLOB_RECURSE BULLET_SOURCES CONFIGURE_DEPENDS ${VIRO_ROOT}/wasm/libs/bullet/src/*.cpp)
  target_sources(viro_sim_host PRIVATE ${BULLET_SOURCES})

  # VRODriverMetal compiles the shader library at runtime from
  # ViroShadersSource.txt next to the executable when no ViroKit bundle or
  # default.metallib exists, which is the case for this host. Generate it the
  # way the visionOS test app does, so it always matches Shaders.metal.
  find_program(RUBY ruby REQUIRED)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ViroShadersSource.txt
    COMMAND ${RUBY} ${VIRO_ROOT}/ios/generate_shader_source.rb
            ${CMAKE_CURRENT_BINARY_DIR}/ViroShadersSource.txt
    DEPENDS ${VIRO_RENDERER_DIR}/Shaders.metal
            ${VIRO_RENDERER_DIR}/VROSharedStructures.h
            ${VIRO_ROOT}/ios/generate_shader_source.rb
    COMMENT "Generating ViroShadersSource.txt")
  add_custom_target(viro_shader_source DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ViroShadersSource.txt)
  add_dependencies(viro_sim_host viro_shader_source)

  # Dead-strip as the visionOS app link does: the GL shader-binding path
  # (VROMaterialShaderBinding -> VROShaderProgram::getUniform) is compiled in
  # but never reached under Metal, and its GL implementation is not built.
  target_link_libraries(viro_sim_host PRIVATE ${FREETYPE_LIBRARIES} "-Wl,-dead_strip")
endif()

target_link_libraries(viro_sim_host PRIVATE
  openxr_loader
  z
  "-framework Metal"
  "-framework MetalKit"
  "-framework Foundation"
  "-framework AppKit"
  "-framework QuartzCore"
  "-framework Accelerate"
  "-framework ImageIO"
  "-framework CoreGraphics"
  "-framework CoreText"
  "-framework CoreVideo"
  "-framework CoreMedia"
  "-framework IOSurface"
  "-framework AVFoundation"
  "-framework AudioToolbox"
  dl
)
