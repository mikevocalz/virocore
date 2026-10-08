// VROOpenXRPlatformAndroid.cpp
// ViroRenderer
//
// Android side of VROOpenXRPlatform.h.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROOpenXRPlatform.h"

#include <cstring>
#include <sys/system_properties.h>
#include "VROPlatformUtil.h"

bool VROOpenXRDebugFlag(const char *name) {
    char value[PROP_VALUE_MAX] = {0};
    return __system_property_get(name, value) > 0 && strcmp(value, "1") == 0;
}

std::string VROOpenXRCacheDirectory() {
    // Render thread, scene running: the platform JNI bridge is up by now.
    return VROPlatformGetCacheDirectory();
}

std::string VROOpenXRBundledAssetPath(const std::string &asset) {
    return VROPlatformCopyAssetToFile(asset);
}
