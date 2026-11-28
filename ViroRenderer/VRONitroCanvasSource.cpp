//
//  VRONitroCanvasSource.cpp
//  ViroRenderer
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license.
//

#include "VRONitroCanvasSource.h"
#include "VRONitroCanvasLookup.h"

namespace {

bool defaultPoll(int32_t surfaceId, NitroCanvasSharedTextureHandle *out) {
    return VRONitroCanvasLookup::lookup(surfaceId, out) == VRONitroCanvasLookupResult::Frame;
}

} // namespace

VRONitroCanvasSource::VRONitroCanvasSource(int32_t surfaceId, VRONitroCanvasPollFn pollFn) :
    _surfaceId(surfaceId),
    _pollFn(pollFn != nullptr ? pollFn : defaultPoll),
    _hasStorage(false),
    _iosurface(0),
    _ahardwareBuffer(0),
    _surfaceTextureGLId(0),
    _width(0),
    _height(0),
    _flags(0),
    _frameCount(0) {
}

bool VRONitroCanvasSource::poll(VRONitroCanvasFrame *out) {
    if (out == nullptr) {
        return false;
    }
    NitroCanvasSharedTextureHandle handle;
    if (!_pollFn(_surfaceId, &handle)) {
        return false;
    }

    const bool same = _hasStorage &&
                      handle.iosurface == _iosurface &&
                      handle.ahardwareBuffer == _ahardwareBuffer &&
                      handle.surfaceTextureGLId == _surfaceTextureGLId &&
                      handle.width == _width &&
                      handle.height == _height &&
                      handle.flags == _flags;

    _hasStorage = true;
    _iosurface = handle.iosurface;
    _ahardwareBuffer = handle.ahardwareBuffer;
    _surfaceTextureGLId = handle.surfaceTextureGLId;
    _width = handle.width;
    _height = handle.height;
    _flags = handle.flags;
    ++_frameCount;

    out->handle = handle;
    out->storageChanged = !same;
    return true;
}
