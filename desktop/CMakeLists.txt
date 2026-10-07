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
# VRO_BRIDGE_FALLBACK: use the hand-rolled bridge pipeline
# (VROSceneGraphMetalBridge) instead of the real VRORenderer + Metal
# substrate path. OFF by default now that the real renderer is wired in.
option(VRO_BRIDGE_FALLBACK "Use bridge renderer instead of real VRORenderer" OFF)

set(VIRO_RENDERER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ViroRenderer)

add_executable(viro_sim_host
  main.mm
  VROSceneRendererMetalOpenXR.mm
  VROPassthroughXR.mm
  VROSimSceneContent.mm
)

if(VIRO_DESKTOP_SCENE)
  target_compile_definitions(viro_sim_host PRIVATE
    VIRO_DESKTOP_SCENE=1
    VRO_METAL=1
    VRO_BRIDGE_FALLBACK=$<BOOL:${VRO_BRIDGE_FALLBACK}>)
  target_include_directories(viro_sim_host PRIVATE
    ${VIRO_RENDERER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../macos/ViroKit
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../wasm/libs/bullet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../wasm/libs/bullet/src)
  target_sources(viro_sim_host PRIVATE
    VROSceneGraphMetalBridge.mm
    VRODriverMetalOpenXR.mm
    VROInputControllerXR.mm
    VRORenderTargetMetalXR.mm)
  # The full render stack: the real VRORenderer + choreographer + portal-tree
  # render pass + scene graph + Metal substrates. Source set mirrors the
  # macos-virocore Xcode target minus tests and asset loaders. All .cpp files
  # are compiled as ObjC++ so VRODefines.h selects VRO_PLATFORM_MACOS and the
  # VRO_METAL=1 headers (which pull in <Metal/Metal.h>) are legal.
  set(VIRO_SOURCES
    ${VIRO_RENDERER_DIR}/VROARConstraintMatcher.cpp
    ${VIRO_RENDERER_DIR}/VROARDeclarativeSession.cpp
    ${VIRO_RENDERER_DIR}/VROARImperativeSession.cpp
    ${VIRO_RENDERER_DIR}/VROARNode.cpp
    ${VIRO_RENDERER_DIR}/VROARScene.cpp
    ${VIRO_RENDERER_DIR}/VROARSceneController.cpp
    ${VIRO_RENDERER_DIR}/VROARShadow.cpp
    ${VIRO_RENDERER_DIR}/VROAction.cpp
    ${VIRO_RENDERER_DIR}/VROAllocationTracker.cpp
    ${VIRO_RENDERER_DIR}/VROAnimatable.cpp
    ${VIRO_RENDERER_DIR}/VROAnimationChain.cpp
    ${VIRO_RENDERER_DIR}/VROAnimationGroup.cpp
    ${VIRO_RENDERER_DIR}/VROBRDFRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROBillboardConstraint.cpp
    ${VIRO_RENDERER_DIR}/VROBone.cpp
    ${VIRO_RENDERER_DIR}/VROBoneUBO.cpp
    ${VIRO_RENDERER_DIR}/VROBoundingBox.cpp
    ${VIRO_RENDERER_DIR}/VROBox.cpp
    ${VIRO_RENDERER_DIR}/VROByteBuffer.cpp
    ${VIRO_RENDERER_DIR}/VROCamera.cpp
    ${VIRO_RENDERER_DIR}/VROChoreographer.cpp
    ${VIRO_RENDERER_DIR}/VROCompress.cpp
    ${VIRO_RENDERER_DIR}/VROConcurrentBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../macos/ViroKit/VROConvert.cpp
    ${VIRO_RENDERER_DIR}/VROData.cpp
    ${VIRO_RENDERER_DIR}/VRODualQuaternion.cpp
    ${VIRO_RENDERER_DIR}/VROEquirectangularToCubeRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROFixedParticleEmitter.cpp
    ${VIRO_RENDERER_DIR}/VROFrameScheduler.cpp
    ${VIRO_RENDERER_DIR}/VROFrameSynchronizerInternal.cpp
    ${VIRO_RENDERER_DIR}/VROFrustum.cpp
    ${VIRO_RENDERER_DIR}/VROFrustumBoxIntersectionMetadata.cpp
    ${VIRO_RENDERER_DIR}/VROFrustumPlane.cpp
    ${VIRO_RENDERER_DIR}/VROGaussianBlurRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROGeometry.cpp
    ${VIRO_RENDERER_DIR}/VROGeometryElement.cpp
    ${VIRO_RENDERER_DIR}/VROGeometrySource.cpp
    ${VIRO_RENDERER_DIR}/VROGeometrySubstrate.cpp
    ${VIRO_RENDERER_DIR}/VROGeometrySubstrateMetal.cpp
    ${VIRO_RENDERER_DIR}/VROGeometrySubstrateOpenGL.cpp
    ${VIRO_RENDERER_DIR}/VROGeometryUtil.cpp
    ${VIRO_RENDERER_DIR}/VROIBLPreprocess.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../macos/ViroKit/VROImageMacOS.cpp
    ${VIRO_RENDERER_DIR}/VROImagePostProcessOpenGL.cpp
    ${VIRO_RENDERER_DIR}/VROImageShaderProgram.cpp
    ${VIRO_RENDERER_DIR}/VROImageUtil.cpp
    ${VIRO_RENDERER_DIR}/VROInputControllerAR.cpp
    ${VIRO_RENDERER_DIR}/VROInputControllerBase.cpp
    ${VIRO_RENDERER_DIR}/VROIrradianceRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROJenkinsHash.cpp
    ${VIRO_RENDERER_DIR}/VROKeyframeAnimation.cpp
    ${VIRO_RENDERER_DIR}/VROLayeredSkeletalAnimation.cpp
    ${VIRO_RENDERER_DIR}/VROLight.cpp
    ${VIRO_RENDERER_DIR}/VROLightingUBO.cpp
    ${VIRO_RENDERER_DIR}/VROLineSegment.cpp
    ${VIRO_RENDERER_DIR}/VROLog.cpp
    ${VIRO_RENDERER_DIR}/VROMaterial.cpp
    ${VIRO_RENDERER_DIR}/VROMaterialShaderBinding.cpp
    ${VIRO_RENDERER_DIR}/VROMaterialSubstrate.cpp
    ${VIRO_RENDERER_DIR}/VROMaterialSubstrateMetal.cpp
    ${VIRO_RENDERER_DIR}/VROMaterialSubstrateOpenGL.cpp
    ${VIRO_RENDERER_DIR}/VROMaterialVisual.cpp
    ${VIRO_RENDERER_DIR}/VROMath.cpp
    ${VIRO_RENDERER_DIR}/VROMatrix4f.cpp
    ${VIRO_RENDERER_DIR}/VROModelIOUtil.cpp
    ${VIRO_RENDERER_DIR}/VRONode.cpp
    ${VIRO_RENDERER_DIR}/VRONodeCamera.cpp
    ${VIRO_RENDERER_DIR}/VROParticleEmitter.cpp
    ${VIRO_RENDERER_DIR}/VROParticleUBO.cpp
    ${VIRO_RENDERER_DIR}/VROPencil.cpp
    ${VIRO_RENDERER_DIR}/VROPhysicsBody.cpp
    ${VIRO_RENDERER_DIR}/VROPhysicsShape.cpp
    ${VIRO_RENDERER_DIR}/VROPhysicsWorld.cpp
    ${VIRO_RENDERER_DIR}/VROPlane.cpp
    ${VIRO_RENDERER_DIR}/VROPlatformUtil.cpp
    ${VIRO_RENDERER_DIR}/VROPolygon.cpp
    ${VIRO_RENDERER_DIR}/VROPolyline.cpp
    ${VIRO_RENDERER_DIR}/VROPortal.cpp
    ${VIRO_RENDERER_DIR}/VROPortalFrame.cpp
    ${VIRO_RENDERER_DIR}/VROPortalTraversalListener.cpp
    ${VIRO_RENDERER_DIR}/VROPortalTreeRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROPostProcessEffectFactory.cpp
    ${VIRO_RENDERER_DIR}/VROPrefilterRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROProjector.cpp
    ${VIRO_RENDERER_DIR}/VROPropertyAnimation.cpp
    ${VIRO_RENDERER_DIR}/VROQuaternion.cpp
    ${VIRO_RENDERER_DIR}/VRORenderTargetOpenGL.cpp
    ${VIRO_RENDERER_DIR}/VRORenderUtil.cpp
    ${VIRO_RENDERER_DIR}/VRORenderer.cpp
    ${VIRO_RENDERER_DIR}/VROReticle.cpp
    ${VIRO_RENDERER_DIR}/VROScene.cpp
    ${VIRO_RENDERER_DIR}/VROSceneController.cpp
    ${VIRO_RENDERER_DIR}/VROShaderCapabilities.cpp
    ${VIRO_RENDERER_DIR}/VROShaderFactory.cpp
    ${VIRO_RENDERER_DIR}/VROShaderModifier.cpp
    ${VIRO_RENDERER_DIR}/VROShaderProgram.cpp
    ${VIRO_RENDERER_DIR}/VROShadowMapRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROShadowPreprocess.cpp
    ${VIRO_RENDERER_DIR}/VROShapeUtils.cpp
    ${VIRO_RENDERER_DIR}/VROSkeletalAnimation.cpp
    ${VIRO_RENDERER_DIR}/VROSkeleton.cpp
    ${VIRO_RENDERER_DIR}/VROSkinner.cpp
    ${VIRO_RENDERER_DIR}/VROSkybox.cpp
    ${VIRO_RENDERER_DIR}/VROSparseBitSet.cpp
    ${VIRO_RENDERER_DIR}/VROSphere.cpp
    ${VIRO_RENDERER_DIR}/VROStringUtil.cpp
    ${VIRO_RENDERER_DIR}/VROSurface.cpp
    ${VIRO_RENDERER_DIR}/VROTaskQueue.cpp
    ${VIRO_RENDERER_DIR}/VROTexture.cpp
    ${VIRO_RENDERER_DIR}/VROTextureReference.cpp
    ${VIRO_RENDERER_DIR}/VROTextureSubstrate.cpp
    ${VIRO_RENDERER_DIR}/VROTextureSubstrateMetal.cpp
    ${VIRO_RENDERER_DIR}/VROTextureSubstrateOpenGL.cpp
    ${VIRO_RENDERER_DIR}/VROTextureUtil.cpp
    ${VIRO_RENDERER_DIR}/VROThreadRestricted.cpp
    ${VIRO_RENDERER_DIR}/VROTime.cpp
    ${VIRO_RENDERER_DIR}/VROTimingFunction.cpp
    ${VIRO_RENDERER_DIR}/VROToneMappingRenderPass.cpp
    ${VIRO_RENDERER_DIR}/VROTorusKnot.cpp
    ${VIRO_RENDERER_DIR}/VROTransaction.cpp
    ${VIRO_RENDERER_DIR}/VROTransformConstraint.cpp
    ${VIRO_RENDERER_DIR}/VROTriangle.cpp
    ${VIRO_RENDERER_DIR}/VROUniform.cpp
    ${VIRO_RENDERER_DIR}/VROVector3f.cpp
    ${VIRO_RENDERER_DIR}/VROVector4f.cpp
    ${VIRO_RENDERER_DIR}/VROVideoSurface.cpp
    ${VIRO_RENDERER_DIR}/VRODriver.cpp
    ${VIRO_RENDERER_DIR}/VRODriverOpenGL.cpp
    ${VIRO_RENDERER_DIR}/VROMorpher.cpp
    ${VIRO_RENDERER_DIR}/VROVertexBufferOpenGL.cpp
    ${VIRO_RENDERER_DIR}/VROARWorldMesh.cpp
    ${VIRO_RENDERER_DIR}/VROARDepthMesh.cpp
    ${VIRO_RENDERER_DIR}/VROBoneConstraint.cpp
    ${VIRO_RENDERER_DIR}/VROIKRig.cpp
    ${VIRO_RENDERER_DIR}/poly2tri/common/shapes.cc
    ${VIRO_RENDERER_DIR}/poly2tri/sweep/advancing_front.cc
    ${VIRO_RENDERER_DIR}/poly2tri/sweep/cdt.cc
    ${VIRO_RENDERER_DIR}/poly2tri/sweep/sweep.cc
    ${VIRO_RENDERER_DIR}/poly2tri/sweep/sweep_context.cc
  )
  # Bullet physics (vendored for wasm; the macos/Libraries binaries are
  # x86-only so we build from source). VROPhysicsBody/Shape/World are linked
  # in by VRONode/VROScene regardless of whether physics is used.
  file(GLOB_RECURSE BULLET_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../wasm/libs/bullet/src/*.cpp)
  target_sources(viro_sim_host PRIVATE ${BULLET_SOURCES})

  # Compile the .cpp Viro sources as Objective-C++ so VRODefines.h selects
  # VRO_PLATFORM_MACOS (its ObjC branch) instead of defaulting to Android.
  set_source_files_properties(${VIRO_SOURCES} PROPERTIES
    COMPILE_FLAGS "-x objective-c++")
  target_sources(viro_sim_host PRIVATE ${VIRO_SOURCES})
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
  "-framework OpenGL"
  "-framework CoreGraphics"
  "-framework CoreText"
  "-framework CoreVideo"
  "-framework AVFoundation"
  "-framework AudioToolbox"
  dl
)
