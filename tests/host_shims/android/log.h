// Host-test stand-in for <android/log.h>. The test defines the function.
#pragma once
enum {
    ANDROID_LOG_VERBOSE = 2,
    ANDROID_LOG_DEBUG = 3,
    ANDROID_LOG_INFO = 4,
    ANDROID_LOG_WARN = 5,
    ANDROID_LOG_ERROR = 6,
};
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
