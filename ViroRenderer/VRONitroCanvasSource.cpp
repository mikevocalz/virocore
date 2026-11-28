//
//  VRONitroCanvasSource.cpp
//  ViroRenderer
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license.
//

#include "VRONitroCanvasSource.h"
#include "VRONitroCanvasLookup.h"

#include <map>
#include <mutex>

// Latest frame published for one surface id, shared by every source bound
// to it. generation 0 means nothing published yet.
struct VRONitroCanvasChannel {
    std::mutex mutex;
    NitroCanvasSharedTextureHandle latest{};
    uint64_t generation = 0;
};

namespace {

std::mutex sChannelsMutex;
std::map<int32_t, std::weak_ptr<VRONitroCanvasChannel>> sChannels;

std::shared_ptr<VRONitroCanvasChannel> channelFor(int32_t surfaceId) {
    std::lock_guard<std::mutex> lock(sChannelsMutex);
    std::weak_ptr<VRONitroCanvasChannel> &slot = sChannels[surfaceId];
    std::shared_ptr<VRONitroCanvasChannel> channel = slot.lock();
    if (!channel) {
        channel = std::make_shared<VRONitroCanvasChannel>();
        slot = channel;
    }
    return channel;
}

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
    _channel = channelFor(surfaceId);
    _seenGeneration = 0;
}

VRONitroCanvasSource::~VRONitroCanvasSource() {
    _channel.reset();
    std::lock_guard<std::mutex> lock(sChannelsMutex);
    auto it = sChannels.find(_surfaceId);
    if (it != sChannels.end() && it->second.expired()) {
        sChannels.erase(it);
    }
}

bool VRONitroCanvasSource::poll(VRONitroCanvasFrame *out) {
    if (out == nullptr) {
        return false;
    }
    NitroCanvasSharedTextureHandle handle;
    {
        std::lock_guard<std::mutex> lock(_channel->mutex);
        NitroCanvasSharedTextureHandle fresh;
        if (_pollFn(_surfaceId, &fresh)) {
            // Ours, fence included. Publish a fence-free copy for the others.
            handle = fresh;
            _channel->latest = fresh;
            _channel->latest.fenceFd = -1;
            ++_channel->generation;
        } else if (_channel->generation != _seenGeneration) {
            // Another source on this id took a frame we have not shown.
            handle = _channel->latest;
        } else {
            return false;
        }
        _seenGeneration = _channel->generation;
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
