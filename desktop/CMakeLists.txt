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

# VIRO_DESKTOP_SCENE: render a real Viro VROBox (ViroRenderer geometry + matrix
# classes) through the Metal path instead of the fallback triangle.
# Verified working against Meta XR Simulator — set OFF for the triangle-only
# minimal build.
option(VIRO_DESKTOP_SCENE "Render VROBox via ViroRenderer classes" ON)

set(VIRO_RENDERER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ViroRenderer)

add_executable(viro_sim_host
  main.mm
  VROSceneRendererMetalOpenXR.mm
)

if(VIRO_DESKTOP_SCENE)
  target_compile_definitions(viro_sim_host PRIVATE VIRO_DESKTOP_SCENE=1)
  target_include_directories(viro_sim_host PRIVATE
    ${VIRO_RENDERER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../macos/ViroKit)
  target_sources(viro_sim_host PRIVATE VROSceneGraphMetalBridge.mm)
  # Viro source subset — expanded as far as compilation allows. These files
  # are compiled as ObjC++ so VRODefines.h selects VRO_PLATFORM_MACOS.
  set(VIRO_SOURCES
    ${VIRO_RENDERER_DIR}/VROBox.cpp
    ${VIRO_RENDERER_DIR}/VROGeometry.cpp
    ${VIRO_RENDERER_DIR}/VROGeometrySource.cpp
    ${VIRO_RENDERER_DIR}/VROGeometryElement.cpp
    ${VIRO_RENDERER_DIR}/VROMatrix4f.cpp
    ${VIRO_RENDERER_DIR}/VROVector3f.cpp
    ${VIRO_RENDERER_DIR}/VROQuaternion.cpp
    ${VIRO_RENDERER_DIR}/VROData.cpp
    ${VIRO_RENDERER_DIR}/VROByteBuffer.cpp
    ${VIRO_RENDERER_DIR}/VROLog.cpp
    ${VIRO_RENDERER_DIR}/VROTime.cpp
    ${VIRO_RENDERER_DIR}/VROAllocationTracker.cpp
    ${VIRO_RENDERER_DIR}/VROThreadRestricted.cpp
    ${VIRO_RENDERER_DIR}/VROMaterial.cpp
    ${VIRO_RENDERER_DIR}/VROMath.cpp
    ${VIRO_RENDERER_DIR}/VROBoundingBox.cpp
    ${VIRO_RENDERER_DIR}/VROShapeUtils.cpp
    # Link closure of the above.
    ${VIRO_RENDERER_DIR}/VROAnimatable.cpp
    ${VIRO_RENDERER_DIR}/VROMaterialVisual.cpp
    ${VIRO_RENDERER_DIR}/VROMorpher.cpp
    ${VIRO_RENDERER_DIR}/VROShaderModifier.cpp
    ${VIRO_RENDERER_DIR}/VROTaskQueue.cpp
    ${VIRO_RENDERER_DIR}/VROTexture.cpp
    ${VIRO_RENDERER_DIR}/VROTransaction.cpp
    ${VIRO_RENDERER_DIR}/VROTriangle.cpp
    ${VIRO_RENDERER_DIR}/VROVector4f.cpp
    ${VIRO_RENDERER_DIR}/VROImageUtil.cpp
    ${VIRO_RENDERER_DIR}/VROStringUtil.cpp
    ${VIRO_RENDERER_DIR}/VROFrameScheduler.cpp
    ${VIRO_RENDERER_DIR}/VROTimingFunction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../macos/ViroKit/VROImageMacOS.cpp
  )
  # Compile the .cpp Viro sources as Objective-C++ so VRODefines.h selects
  # VRO_PLATFORM_MACOS (its ObjC branch) instead of defaulting to Android.
  set_source_files_properties(${VIRO_SOURCES} PROPERTIES
    COMPILE_FLAGS "-x objective-c++")
  target_sources(viro_sim_host PRIVATE ${VIRO_SOURCES})
endif()

target_link_libraries(viro_sim_host PRIVATE
  openxr_loader
  "-framework Metal"
  "-framework Foundation"
  "-framework AppKit"
  "-framework QuartzCore"
  "-framework Accelerate"
  "-framework ImageIO"
  dl
)
