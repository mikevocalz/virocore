//
//  Object_JNI.cpp
//  ViroRenderer
//
//  Copyright © 2016 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <memory>
#include <VROModelIOUtil.h>
#include <VROGLTFLoader.h>
#include "VROOBJLoader.h"
#include "VROFBXLoader.h"
#include "VROGeometry.h"
#include "VRONode.h"
#include "VROMorpher.h"
#include "VROSkinner.h"
#include "VROSkeleton.h"
#include "VROBone.h"
#include "Node_JNI.h"
#include "OBJLoaderDelegate_JNI.h"
#include "ViroContext_JNI.h"
#include "VRODefines.h"
#include VRO_C_INCLUDE
#include "Geometry_JNI.h"

#if VRO_PLATFORM_ANDROID
#define VRO_METHOD(return_type, method_name) \
    JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_Object3D_##method_name
#else
#define VRO_METHOD(return_type, method_name) \
    return_type Object3D_##method_name
#endif

extern "C" {

VRO_METHOD(void, nativeLoadModelFromURL)(VRO_ARGS
                                         VRO_STRING jURL,
                                         VRO_REF(VRONode) node_j,
                                         VRO_REF(ViroContext) context_j,
                                         VRO_INT jModelType,
                                         VRO_LONG requestId) {
    VRO_METHOD_PREAMBLE;
    VROPlatformSetEnv(env); // Invoke in case renderer has not yet initialized
    ModelType modelType = VROGetModelType(jModelType);
    std::string URL = VRO_STRING_STL(jURL);

    std::shared_ptr<OBJLoaderDelegate> delegateRef = std::make_shared<OBJLoaderDelegate>(obj, env);
    std::function<void(std::shared_ptr<VRONode> node, bool success)> onFinish =
            [delegateRef, modelType, requestId](std::shared_ptr<VRONode> node, bool success) {
                if (!success) {
                    delegateRef->objFailed("Failed to load model");
                }
                else {
                    delegateRef->objLoaded(node, modelType, requestId);
                }
            };

    std::shared_ptr<VRONode> node = VRO_REF_GET(VRONode, node_j);
    std::shared_ptr<ViroContext> context = VRO_REF_GET(ViroContext, context_j);
    std::shared_ptr<VRODriver> driver = context->getDriver();

    VROPlatformDispatchAsyncRenderer([modelType, node, URL, driver, onFinish] {
        if (modelType == ModelType::FBX) {
            VROFBXLoader::loadFBXFromResource(URL, VROResourceType::URL, node, driver, onFinish);
        } else if (modelType == ModelType::OBJ) {
            VROOBJLoader::loadOBJFromResource(URL, VROResourceType::URL, node, driver, onFinish);
        } else if (modelType == ModelType::GLTF || modelType == ModelType::GLB) {
            VROGLTFLoader::loadGLTFFromResource(URL, {}, VROResourceType::URL,
                                                node, modelType == ModelType::GLB, driver, onFinish);
        } else {
            perr("Viro: Attempted to load unsupported model type within Object_JNI.");
            onFinish(nullptr, false);
        }
    });
}

VRO_METHOD(void, nativeLoadModelFromResources)(VRO_ARGS
                                               VRO_STRING jresource,
                                               VRO_OBJECT resourceMap_j,
                                               VRO_REF(VRONode) node_j,
                                               VRO_REF(ViroContext) context_j,
                                               VRO_INT jModelType,
                                               VRO_LONG requestId) {
    VRO_METHOD_PREAMBLE;
    VROPlatformSetEnv(env); // Invoke in case renderer has not yet initialized
    ModelType modelType = VROGetModelType(jModelType);
    std::string resource = VRO_STRING_STL(jresource);

    std::shared_ptr<OBJLoaderDelegate> delegateRef = std::make_shared<OBJLoaderDelegate>(obj, env);
    std::function<void(std::shared_ptr<VRONode> node, bool success)> onFinish =
            [delegateRef, modelType, requestId](std::shared_ptr<VRONode> node, bool success) {
                if (!success) {
                    delegateRef->objFailed("Failed to load model");
                }
                else {
                    delegateRef->objLoaded(node, modelType, requestId);
                }
            };

    std::map<std::string, std::string> resourceMap;
    bool hasResourceMap = false;
    if (!VRO_IS_OBJECT_NULL(resourceMap_j)) {
        resourceMap = VROPlatformConvertFromJavaMap(resourceMap_j);
        hasResourceMap = true;
    }

    std::shared_ptr<VRONode> node = VRO_REF_GET(VRONode, node_j);
    std::shared_ptr<ViroContext> context = VRO_REF_GET(ViroContext, context_j);
    std::shared_ptr<VRODriver> driver = context->getDriver();

    VROPlatformDispatchAsyncRenderer([modelType, resource, hasResourceMap, resourceMap, node, driver, onFinish] {
        if (modelType == ModelType::FBX) {
            if (!hasResourceMap) {
                VROFBXLoader::loadFBXFromResource(resource, VROResourceType::BundledResource, node,
                                                  driver, onFinish);
            } else {
                VROFBXLoader::loadFBXFromResources(resource, VROResourceType::BundledResource, node,
                                                   resourceMap, driver, onFinish);
            }
        } else if (modelType == ModelType::OBJ) {
            if (!hasResourceMap) {
                VROOBJLoader::loadOBJFromResource(resource, VROResourceType::BundledResource, node,
                                                  driver, onFinish);
            } else {
                VROOBJLoader::loadOBJFromResources(resource, VROResourceType::BundledResource, node,
                                                   resourceMap, driver, onFinish);
            }
        } else if (modelType == ModelType::GLTF || modelType == ModelType::GLB) {
            VROGLTFLoader::loadGLTFFromResource(resource, resourceMap, VROResourceType::BundledResource,
                                                node, modelType == ModelType::GLB,
                                                driver, onFinish);
        } else {
            perr("Viro: Attempted to load unsupported model type within Object_JNI.");
            onFinish(nullptr, false);
        }
    });
}

VRO_METHOD(VRO_OBJECT_ARRAY, nativeCreateChildNodes)(VRO_ARGS
                                      VRO_REF(VRONode) native_parent_node_ref) {
    VRO_METHOD_PREAMBLE;

    // Invoke in case renderer has not yet initialized
    VROPlatformSetEnv(env);

    // Grab the current node.
    std::shared_ptr<VRONode> node = VRO_REF_GET(VRONode, native_parent_node_ref);

    // Create an empty jNode for each child nodes.
    VRO_OBJECT_ARRAY nodeArray = VRO_NEW_OBJECT_ARRAY(node->getChildNodes().size(), "com/viro/core/Node");
    for (int i = 0; i < node->getChildNodes().size(); i ++) {

        // Create a persistent native reference that would represent the jNode object.
        std::shared_ptr<VRONode> childNode = node->getChildNodes()[i];
        VRO_REF(VRONode) nodeRef = VRO_REF_NEW(VRONode, childNode);

        // Construct our Node.java object with that native reference.
        VRO_OBJECT jNode = VROPlatformConstructHostObject("com/viro/core/Node", "(Z)V", false);
        VROPlatformCallHostFunction(jNode, "initWithNativeRef", "(J)V", nodeRef);

        VRO_ARRAY_SET(nodeArray, i, jNode);
    }

    return nodeArray;
}

VRO_METHOD(void, nativeIntializeNode)(VRO_ARGS
                                      VRO_OBJECT jNode,
                                      VRO_REF(VRONode) native_node_ref) {
    VRO_METHOD_PREAMBLE;

    // Invoke in case renderer has not yet initialized
    VROPlatformSetEnv(env);

    // Set the basic properties on a Node.java object.
    std::shared_ptr<VRONode> node = VRO_REF_GET(VRONode, native_node_ref);
    VROPlatformSetString(env, jNode, "mName",    node->getName());
    VROPlatformSetString(env, jNode, "mTag",     node->getTag());
    VROPlatformSetFloat(env,  jNode, "mOpacity", node->getOpacity());
    VROPlatformSetBool(env,   jNode, "mVisible", node->isVisible());

    // Create and set a jGeom representing the node's geometry.
    std::shared_ptr<VROGeometry> geom = node->getGeometry();
    if (geom) {
       VRO_OBJECT jgeom = Geometry::createJGeometry(geom);
       VROPlatformSetObject(env, jNode, "mGeometry", "Lcom/viro/core/Geometry;", jgeom);
       VRO_DELETE_LOCAL_REF(jgeom);
    }
}

VRO_METHOD(void, nativeSetMorphTargetWithWeight)(VRO_ARGS
                                                 VRO_REF(VRONode) native_node_ref,
                                                 VRO_STRING jTarget,
                                                 VRO_FLOAT jWeight) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<VRONode> node_w = VRO_REF_GET(VRONode, native_node_ref);
    std::string target = VRO_STRING_STL(jTarget);

    VROPlatformDispatchAsyncRenderer([node_w, target, jWeight] {
        std::shared_ptr<VRONode> node = node_w.lock();
        if (node) {
            // Set the target's weight for all VROMorpher containing the matching jTarget key
            // in this 3D model.
            std::set<std::shared_ptr<VROMorpher>> morphers = node->getMorphers(true);
            for (auto morpher : morphers) {
                morpher->setWeightForTarget(target, jWeight);
            }
        }
    });
}

VRO_METHOD(VRO_STRING_ARRAY, nativeGetMorphTargetKeys)(VRO_ARGS
                                                       VRO_REF(VRONode) nativeRef) {
    std::shared_ptr<VRONode> node = VRO_REF_GET(VRONode, nativeRef);

    // Iterate through each morph target in this model to create a list of morph keys.
    std::set<std::shared_ptr<VROMorpher>> morphers = node->getMorphers(true);
    std::set<std::string> keys;
    for (auto morpher : morphers) {
        std::set<std::string> morphKeys = morpher->getMorphTargetKeys();
        keys.insert(morphKeys.begin(), morphKeys.end());
    }

    // Pack the result up into a jString array and return
    VRO_STRING_ARRAY array = VRO_NEW_STRING_ARRAY(keys.size());
    int i = 0;
    for (const std::string &key : keys) {
        VRO_STRING_ARRAY_SET(array, i, key);
        ++i;
    }
    return array;
}

VRO_METHOD(void, nativeSetMorphTargetWeights)(VRO_ARGS
                                              VRO_REF(VRONode) nativeRef,
                                              VRO_STRING_ARRAY jTargets,
                                              VRO_FLOAT_ARRAY jWeights) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<VRONode> node_w = VRO_REF_GET(VRONode, nativeRef);

    // Copy the names and weights out on the calling thread — the render-thread
    // block must not touch JNI handles.
    int count = VRO_ARRAY_LENGTH(jTargets);
    std::vector<std::pair<std::string, float>> weights;
    weights.reserve(count);
    VRO_FLOAT *weightArray = VRO_FLOAT_ARRAY_GET_ELEMENTS(jWeights);
    for (int i = 0; i < count; i++) {
        VRO_STRING jTarget = (VRO_STRING) VRO_STRING_ARRAY_GET(jTargets, i);
        weights.emplace_back(VRO_STRING_STL(jTarget), weightArray[i]);
    }
    VRO_FLOAT_ARRAY_RELEASE_ELEMENTS(jWeights, weightArray);

    // One dispatch for the whole face. The per-target setter dispatches once per
    // shape, which is 52 render-thread blocks a frame for an avatar; this is the
    // same work as one.
    VROPlatformDispatchAsyncRenderer([node_w, weights] {
        std::shared_ptr<VRONode> node = node_w.lock();
        if (node == nullptr) {
            return;
        }
        std::set<std::shared_ptr<VROMorpher>> morphers = node->getMorphers(true);
        for (auto &entry : weights) {
            for (auto morpher : morphers) {
                morpher->setWeightForTarget(entry.first, entry.second);
            }
        }
    });
}

/*
 The first skeleton at or below this node. A GLB avatar is one skin; a model
 with several would need a skeleton index added to this API — returning the
 first is the honest behaviour for the models this seam exists for.
 */
static std::shared_ptr<VROSkeleton> VROGetSkeleton(std::shared_ptr<VRONode> node) {
    if (node == nullptr) {
        return nullptr;
    }
    std::vector<std::shared_ptr<VROSkinner>> skinners;
    node->getSkinner(skinners, true);
    if (skinners.empty()) {
        return nullptr;
    }
    return skinners.front()->getSkeleton();
}

VRO_METHOD(VRO_STRING_ARRAY, nativeGetSkeletonBoneKeys)(VRO_ARGS
                                                        VRO_REF(VRONode) nativeRef) {
    std::shared_ptr<VRONode> node = VRO_REF_GET(VRONode, nativeRef);
    std::shared_ptr<VROSkeleton> skeleton = VROGetSkeleton(node);
    if (skeleton == nullptr) {
        return VRO_NEW_STRING_ARRAY(0);
    }

    int count = skeleton->getNumBones();
    VRO_STRING_ARRAY array = VRO_NEW_STRING_ARRAY(count);
    for (int i = 0; i < count; i++) {
        VRO_STRING_ARRAY_SET(array, i, skeleton->getBone(i)->getName());
    }
    return array;
}

VRO_METHOD(VRO_FLOAT_ARRAY, nativeGetSkeletonBoneWorldTransform)(VRO_ARGS
                                                                 VRO_REF(VRONode) nativeRef,
                                                                 VRO_STRING jBoneName) {
    VRO_METHOD_PREAMBLE;
    std::shared_ptr<VRONode> node = VRO_REF_GET(VRONode, nativeRef);
    std::shared_ptr<VROSkeleton> skeleton = VROGetSkeleton(node);
    std::string boneName = VRO_STRING_STL(jBoneName);
    if (skeleton == nullptr || skeleton->getBone(boneName) == nullptr) {
        return VRO_NEW_FLOAT_ARRAY(0);
    }

    // Read on the calling thread. The transform is a snapshot for composing
    // deltas against — the rest pose in practice, read once before driving.
    VROMatrix4f transform = skeleton->getCurrentBoneWorldTransform(boneName);
    VRO_FLOAT_ARRAY array = VRO_NEW_FLOAT_ARRAY(16);
    VRO_FLOAT_ARRAY_SET(array, 0, 16, transform.getArray());
    return array;
}

VRO_METHOD(void, nativeSetSkeletonBoneWorldTransforms)(VRO_ARGS
                                                       VRO_REF(VRONode) nativeRef,
                                                       VRO_STRING_ARRAY jNames,
                                                       VRO_FLOAT_ARRAY jMatrices,
                                                       VRO_BOOL jRecurse) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<VRONode> node_w = VRO_REF_GET(VRONode, nativeRef);

    int count = VRO_ARRAY_LENGTH(jNames);
    if (VRO_ARRAY_LENGTH(jMatrices) != count * 16) {
        return;
    }

    std::vector<std::pair<std::string, VROMatrix4f>> transforms;
    transforms.reserve(count);
    VRO_FLOAT *matrixArray = VRO_FLOAT_ARRAY_GET_ELEMENTS(jMatrices);
    for (int i = 0; i < count; i++) {
        VRO_STRING jName = (VRO_STRING) VRO_STRING_ARRAY_GET(jNames, i);
        transforms.emplace_back(VRO_STRING_STL(jName), VROMatrix4f(matrixArray + i * 16));
    }
    VRO_FLOAT_ARRAY_RELEASE_ELEMENTS(jMatrices, matrixArray);
    bool recurse = jRecurse;

    // Bones are VROAnimatable state — written on the rendering thread, the same
    // rule the morph setters follow. One dispatch carries the whole pose.
    VROPlatformDispatchAsyncRenderer([node_w, transforms, recurse] {
        std::shared_ptr<VRONode> node = node_w.lock();
        std::shared_ptr<VROSkeleton> skeleton = VROGetSkeleton(node);
        if (skeleton == nullptr) {
            return;
        }
        for (auto &entry : transforms) {
            skeleton->setCurrentBoneWorldTransform(entry.first, entry.second, recurse);
        }
    });
}

VRO_METHOD(void, nativeSetMorphMode)(VRO_ARGS
                                     VRO_REF(VRONode) nativeRef,
                                     VRO_STRING jMode) {

    std::weak_ptr<VRONode> node_w = VRO_REF_GET(VRONode, nativeRef);
    std::string modeString = VRO_STRING_STL(jMode);
    VROPlatformDispatchAsyncRenderer([node_w, modeString] {
        std::shared_ptr<VRONode> node = node_w.lock();
        if (node == nullptr){
            return;
        }

        VROMorpher::ComputeLocation mode = VROMorpher::ComputeLocation::CPU;
        if (VROStringUtil::strcmpinsensitive("gpu", modeString)) {
            mode = VROMorpher::ComputeLocation::GPU;
        } else if (VROStringUtil::strcmpinsensitive("hybrid", modeString)) {
            mode = VROMorpher::ComputeLocation::Hybrid;
        }

        std::set<std::shared_ptr<VROMorpher>> morphers = node->getMorphers(true);
        for (auto morpher : morphers) {
            if (!morpher->setComputeLocation(mode)) {
                pwarn("Unable to set render mode %s.", modeString.c_str());
            }
        }
    });
}

} // extern "C"