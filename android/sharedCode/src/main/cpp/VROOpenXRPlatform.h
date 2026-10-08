// VROOpenXRPlatform.h
// ViroRenderer
//
// The few host services the OpenXR input code (VROInputControllerOpenXR,
// VROOpenXRRenderModel, VROInputPresenterOpenXR) needs beyond OpenXR itself.
// Android (Quest, PICO, Android XR) implements them in
// VROOpenXRPlatformAndroid.cpp; the macOS simulator host implements them in
// desktop/VROOpenXRPlatformDesktop.mm. Keeping them behind this header lets
// the desktop host compile and run the input logic that ships on headsets
// instead of a copy of it.
//
// Logging: define LOG_TAG before including this header, then use
// ALOGE/ALOGW/ALOGI/ALOGV as on Android.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROOPENXRPLATFORM_H
#define ANDROID_VROOPENXRPLATFORM_H

#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#else
// Desktop: one line per message on stdout, tagged like logcat, so the gate
// script reads the same diagnostic lines a device log carries.
void VROOpenXRLog(char level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
#define ALOGE(...) VROOpenXRLog('E', LOG_TAG, __VA_ARGS__)
#define ALOGW(...) VROOpenXRLog('W', LOG_TAG, __VA_ARGS__)
#define ALOGI(...) VROOpenXRLog('I', LOG_TAG, __VA_ARGS__)
#define ALOGV(...) VROOpenXRLog('V', LOG_TAG, __VA_ARGS__)
#endif

/*
 True when the debug switch `name` (an Android system property such as
 "debug.viro.fake_gaze") is set to "1". Android reads the property; the
 desktop host reads the environment variable with dots replaced by
 underscores and upper-cased (DEBUG_VIRO_FAKE_GAZE). Callers compile this out
 of release builds.
 */
bool VROOpenXRDebugFlag(const char *name);

/*
 Writable directory for files the runtime hands back (XR_FB_render_model
 GLBs). Render thread.
 */
std::string VROOpenXRCacheDirectory();

/*
 Local file path for an asset bundled with the app (Android: copied out of
 the APK assets; desktop: next to the executable). Empty when missing.
 */
std::string VROOpenXRBundledAssetPath(const std::string &asset);

#endif  // ANDROID_VROOPENXRPLATFORM_H
