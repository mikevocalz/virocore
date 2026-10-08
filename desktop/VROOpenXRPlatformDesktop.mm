//
//  VROOpenXRPlatformDesktop.mm
//
//  macOS side of android/sharedCode/src/main/cpp/VROOpenXRPlatform.h, so the
//  simulator host runs the shared VROInputControllerOpenXR unchanged.
//
//  SOT-KEYWORDS: openxr, macos, simulator, platform-seam
//

#include "VROOpenXRPlatform.h"

#import <Foundation/Foundation.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

void VROOpenXRLog(char level, const char *tag, const char *format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    // Background loads log too; one locked write keeps lines whole.
    static std::mutex sLogMutex;
    std::lock_guard<std::mutex> lock(sLogMutex);
    std::fprintf(stdout, "%c %s: %s\n", level, tag, message);
}

bool VROOpenXRDebugFlag(const char *name) {
    std::string env(name);
    for (char &c : env) {
        c = (c == '.') ? '_' : (char)std::toupper((unsigned char)c);
    }
    const char *value = getenv(env.c_str());
    return value && std::string(value) == "1";
}

std::string VROOpenXRCacheDirectory() {
    @autoreleasepool {
        std::string dir = std::string([NSTemporaryDirectory() UTF8String]) + "viro-openxr-cache";
        if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
            return "";
        }
        return dir;
    }
}

std::string VROOpenXRBundledAssetPath(const std::string &asset) {
    // CMake copies bundled assets next to viro_sim_host.
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> exe(size + 1, '\0');
    if (_NSGetExecutablePath(exe.data(), &size) != 0) {
        return "";
    }
    std::string path(exe.data());
    const size_t slash = path.rfind('/');
    path = (slash == std::string::npos ? std::string(".") : path.substr(0, slash)) + "/" + asset;
    return access(path.c_str(), R_OK) == 0 ? path : std::string();
}
