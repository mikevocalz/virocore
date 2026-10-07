//
//  VRODriverMetal.h
//  ViroRenderer
//
//  Metal implementation of the Viro rendering driver. Rewritten (2025) for the
//  modern VRODriver interface: the previous revision implemented only ~8 of
//  ~30 pure virtuals and referenced an obsolete substrate API.
//
//  This base class provides:
//    - Metal device / command-queue / shader-library accessors
//    - Complete but inert implementations of every VRODriver pure virtual,
//      so concrete drivers only override what they can actually honour
//    - CPU-tracked render state (cull, blend, depth, colour mask) that the
//      Metal substrates bake into MTLRenderPipelineState / MTLDepthStencilState
//      objects at draw time
//    - Command-buffer / render-encoder plumbing: the render target that is
//      bound opens a MTLRenderCommandEncoder from the driver's frame command
//      buffer; substrates fetch it via getActiveRenderEncoder()
//
//  Concrete drivers (e.g. desktop/VRODriverMetalOpenXR) override the factory
//  and lifecycle methods and supply the display render target.
//
//  SOT-KEYWORDS: metal, driver, substrate
//

#ifndef VRODriverMetal_h
#define VRODriverMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include <stdio.h>
#include <memory>
#include <string>
#include <Metal/Metal.h>

#include "VRODriver.h"
#include "VROMaterial.h" // for VROBlendMode / VROCullMode enum definitions
#include "VROVertexBuffer.h"
#include "VROFrameScheduler.h"
#include "VRORenderTarget.h"
#include "VROLog.h"

class VROGeometrySubstrate;
class VROMaterialSubstrate;
class VROTextureSubstrate;

/*
 CPU-backed vertex buffer: Metal geometry substrates read the underlying
 VROData directly and create their own MTLBuffer, so hydrate() is a no-op.
 */
class VROVertexBufferMetal final : public VROVertexBuffer {
public:
    explicit VROVertexBufferMetal(std::shared_ptr<VROData> data) : VROVertexBuffer(data) {}
    void hydrate() override {}
};

/*
 Driver for Metal.

 All VRODriver virtuals have an implementation here; most state-mutating
 calls simply record the requested state (Metal bakes state into pipeline
 objects rather than exposing global toggles). Subclasses override the
 lifecycle hooks, the display/render-target factories, and the media
 factories as needed.
 */
class VRODriverMetal : public VRODriver, public std::enable_shared_from_this<VRODriverMetal> {

public:

    VRODriverMetal(id <MTLDevice> device, id <MTLCommandQueue> commandQueue) :
        _device(device),
        _commandQueue(commandQueue),
        _scheduler(std::make_shared<VROFrameScheduler>()),
        _colorPixelFormat(MTLPixelFormatBGRA8Unorm),
        _depthPixelFormat(MTLPixelFormatDepth32Float),
        _stencilPixelFormat(MTLPixelFormatInvalid),
        _sampleCount(1),
        _frameCommandBuffer(nil),
        _activeEncoder(nil),
        _cullMode(VROCullMode::Back),
        _blendMode(VROBlendMode::Alpha),
        _depthWritingEnabled(true),
        _depthReadingEnabled(true),
        _stencilTestEnabled(false),
        _renderTargetColorWritingMask(VROColorMaskAll),
        _materialColorWritingMask(VROColorMaskAll) {
        if (_commandQueue == nil) {
            _commandQueue = [_device newCommandQueue];
        }
    }

    virtual ~VRODriverMetal() {}

#pragma mark - Metal accessors

    id <MTLDevice> getDevice() const {
        return _device;
    }
    id <MTLCommandQueue> getCommandQueue() const {
        return _commandQueue;
    }
    id <MTLLibrary> getLibrary() const {
        return _library;
    }
    void setLibrary(id <MTLLibrary> library) {
        _library = library;
    }

    id <MTLLibrary> newLibraryWithSource(std::string source) {
        NSError *error = nil;
        id <MTLLibrary> library = [_device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()]
                                                      options:nil
                                                        error:&error];
        if (!library) {
            pinfo("VRODriverMetal: shader library compile failed: %s",
                  error ? [[error localizedDescription] UTF8String] : "?");
        }
        return library;
    }

#pragma mark - Pixel formats

    MTLPixelFormat getColorPixelFormat() const {
        return _colorPixelFormat;
    }
    void setColorPixelFormat(MTLPixelFormat format) {
        _colorPixelFormat = format;
    }
    MTLPixelFormat getDepthPixelFormat() const {
        return _depthPixelFormat;
    }
    void setDepthPixelFormat(MTLPixelFormat format) {
        _depthPixelFormat = format;
    }
    MTLPixelFormat getStencilPixelFormat() const {
        return _stencilPixelFormat;
    }
    void setStencilPixelFormat(MTLPixelFormat format) {
        _stencilPixelFormat = format;
    }
    NSUInteger getSampleCount() const {
        return _sampleCount;
    }
    void setSampleCount(NSUInteger samples) {
        _sampleCount = samples;
    }

#pragma mark - Frame command buffer / render encoder plumbing

    /*
     The render loop supplies the frame's command buffer before any eye is
     rendered. Every render command encoder is opened from it, because Metal
     permits only one encoder per command buffer at a time.
     */
    void setFrameCommandBuffer(id <MTLCommandBuffer> commandBuffer) {
        _frameCommandBuffer = commandBuffer;
    }
    id <MTLCommandBuffer> getFrameCommandBuffer() const {
        return _frameCommandBuffer;
    }

    /*
     The currently open render command encoder, set by the render target when
     it is bound. Substrates draw through this encoder.
     */
    void setActiveRenderEncoder(id <MTLRenderCommandEncoder> encoder) {
        _activeEncoder = encoder;
    }
    id <MTLRenderCommandEncoder> getActiveRenderEncoder() const {
        return _activeEncoder;
    }
    bool hasOpenEncoder() const {
        return _activeEncoder != nil;
    }
    void endActiveEncoder() {
        if (_activeEncoder != nil) {
            [_activeEncoder endEncoding];
            _activeEncoder = nil;
        }
    }

#pragma mark - CPU-tracked render state (read by substrates)

    VROCullMode getCullMode() const { return _cullMode; }
    VROBlendMode getBlendMode() const { return _blendMode; }
    bool isDepthWritingEnabled() const { return _depthWritingEnabled; }
    bool isDepthReadingEnabled() const { return _depthReadingEnabled; }
    VROColorMask getRenderTargetColorWritingMask() const { return _renderTargetColorWritingMask; }
    VROColorMask getMaterialColorWritingMask() const { return _materialColorWritingMask; }

#pragma mark - VRODriver: frame / eye lifecycle

    void willRenderFrame(const VRORenderContext &context) override {}
    void didRenderFrame(const VROFrameTimer &timer, const VRORenderContext &context) override {}
    void willRenderEye(const VRORenderContext &context) override {}
    void didRenderEye(const VRORenderContext &context) override {}
    void pause() override {}
    void resume() override {}

    void readGPUType() override {}
    VROGPUType getGPUType() override { return _gpuType; }
    void setGPUType(VROGPUType type) { _gpuType = type; }

    void readDisplayFramebuffer() override {}

#pragma mark - VRODriver: texture-unit / shader binding (GL concepts, no-op)

    void setActiveTextureUnit(int unit) override {}
    void bindTexture(int target, int texture) override {}
    void bindTexture(int unit, int target, int texture) override {}
    void bindShader(std::shared_ptr<VROShaderProgram> program) override {}
    void unbindShader() override {}

#pragma mark - VRODriver: render state (recorded for pipeline baking)

    void setDepthWritingEnabled(bool enabled) override { _depthWritingEnabled = enabled; }
    void setDepthReadingEnabled(bool enabled) override { _depthReadingEnabled = enabled; }
    void setStencilTestEnabled(bool enabled) override { _stencilTestEnabled = enabled; }

    void setCullMode(VROCullMode cullMode) override {
        _cullMode = cullMode;
        if (_activeEncoder != nil) {
            [_activeEncoder setCullMode:toMTLCullMode(cullMode)];
        }
    }

    void setRenderTargetColorWritingMask(VROColorMask mask) override {
        _renderTargetColorWritingMask = mask;
    }
    void setMaterialColorWritingMask(VROColorMask mask) override {
        _materialColorWritingMask = mask;
    }
    void setBlendingMode(VROBlendMode mode) override {
        _blendMode = mode;
    }

    static MTLCullMode toMTLCullMode(VROCullMode cullMode) {
        switch (cullMode) {
            case VROCullMode::Back:  return MTLCullModeBack;
            case VROCullMode::Front: return MTLCullModeFront;
            case VROCullMode::None:  return MTLCullModeNone;
        }
        return MTLCullModeBack;
    }

#pragma mark - VRODriver: render targets

    /*
     Binds the given render target: ends the active encoder, invalidates the
     previously bound target per the unbind op, then binds the new target
     (which opens a fresh encoder from the frame command buffer).
     */
    bool bindRenderTarget(std::shared_ptr<VRORenderTarget> target,
                          VRORenderTargetUnbindOp unbindOp) override {
        if (target == _boundTarget) {
            return false;
        }
        endActiveEncoder();
        if (_boundTarget) {
            _boundTarget->invalidate();
        }
        _boundTarget = target;
        if (_boundTarget) {
            _boundTarget->bind();
        }
        return true;
    }

    void unbindRenderTarget() override {
        endActiveEncoder();
        if (_boundTarget) {
            _boundTarget->invalidate();
            _boundTarget = nullptr;
        }
    }

    std::shared_ptr<VRORenderTarget> getRenderTarget() override {
        return _boundTarget ? _boundTarget : getDisplay();
    }

    std::shared_ptr<VRORenderTarget> getBoundRenderTarget() const {
        return _boundTarget;
    }

#pragma mark - VRODriver: colour pipeline

    VROColorRenderingMode getColorRenderingMode() override {
        return VROColorRenderingMode::NonLinear;
    }
    void setHasSoftwareGammaPass(bool softwareGamma) override {}
    bool hasSoftwareGammaPass() const override { return false; }
    bool isBloomSupported() override { return false; }

#pragma mark - VRODriver: substrate factories (overridden or subclass-provided)

    VROGeometrySubstrate *newGeometrySubstrate(const VROGeometry &geometry) override;
    VROMaterialSubstrate *newMaterialSubstrate(VROMaterial &material) override;
    VROTextureSubstrate *newTextureSubstrate(VROTextureType type,
                                             VROTextureFormat format,
                                             VROTextureInternalFormat internalFormat, bool sRGB,
                                             VROMipmapMode mipmapMode,
                                             std::vector<std::shared_ptr<VROData>> &data,
                                             int width, int height, std::vector<uint32_t> mipSizes,
                                             VROWrapMode wrapS, VROWrapMode wrapT,
                                             VROFilterMode minFilter, VROFilterMode magFilter,
                                             VROFilterMode mipFilter) override;

    std::shared_ptr<VROVertexBuffer> newVertexBuffer(std::shared_ptr<VROData> data) override {
        return std::make_shared<VROVertexBufferMetal>(data);
    }

    std::shared_ptr<VRORenderTarget> newRenderTarget(VRORenderTargetType type, int numAttachments,
                                                     int numImages, bool enableMipmaps,
                                                     bool needsDepthStencil) override {
        pwarn("VRODriverMetal::newRenderTarget not implemented in base driver");
        return nullptr;
    }

    std::shared_ptr<VRORenderTarget> getDisplay() override {
        return _displayTarget;
    }
    void setDisplayTarget(std::shared_ptr<VRORenderTarget> target) {
        _displayTarget = target;
    }

    std::shared_ptr<VROImagePostProcess> newImagePostProcess(std::shared_ptr<VROShaderProgram> shader) override {
        pwarn("VRODriverMetal::newImagePostProcess not implemented (post-processing unsupported)");
        return nullptr;
    }

    std::shared_ptr<VROVideoTextureCache> newVideoTextureCache() override {
        pwarn("VRODriverMetal::newVideoTextureCache unsupported");
        return nullptr;
    }

#pragma mark - VRODriver: audio / text (unsupported on base Metal driver)

    std::shared_ptr<VROSound> newSound(std::shared_ptr<VROSoundData> data, VROSoundType type) override {
        return nullptr;
    }
    std::shared_ptr<VROSound> newSound(std::string resource, VROResourceType resourceType,
                                       VROSoundType type) override {
        return nullptr;
    }
    std::shared_ptr<VROAudioPlayer> newAudioPlayer(std::shared_ptr<VROSoundData> data) override {
        return nullptr;
    }
    std::shared_ptr<VROAudioPlayer> newAudioPlayer(std::string path, bool isLocal) override {
        return nullptr;
    }
    std::shared_ptr<VROTypefaceCollection> newTypefaceCollection(std::string typefaces, int size,
                                                                 VROFontStyle style,
                                                                 VROFontWeight weight) override {
        pwarn("VRODriverMetal::newTypefaceCollection unsupported (no text rendering)");
        return nullptr;
    }
    void setSoundRoom(float sizeX, float sizeY, float sizeZ, std::string wallMaterial,
                      std::string ceilingMaterial, std::string floorMaterial) override {}

    std::shared_ptr<VROFrameScheduler> getFrameScheduler() override {
        return _scheduler;
    }
    void *getGraphicsContext() override {
        return nullptr;
    }

protected:

    id <MTLDevice> _device;
    id <MTLCommandQueue> _commandQueue;
    id <MTLLibrary> _library;

    MTLPixelFormat _colorPixelFormat;
    MTLPixelFormat _depthPixelFormat;
    MTLPixelFormat _stencilPixelFormat;
    NSUInteger _sampleCount;

    id <MTLCommandBuffer> _frameCommandBuffer;
    id <MTLRenderCommandEncoder> _activeEncoder;

    VROGPUType _gpuType = VROGPUType::Normal;

    VROCullMode _cullMode;
    VROBlendMode _blendMode;
    bool _depthWritingEnabled;
    bool _depthReadingEnabled;
    bool _stencilTestEnabled;
    VROColorMask _renderTargetColorWritingMask;
    VROColorMask _materialColorWritingMask;

    std::shared_ptr<VRORenderTarget> _boundTarget;
    std::shared_ptr<VRORenderTarget> _displayTarget;
    std::shared_ptr<VROFrameScheduler> _scheduler;

};

#include "VROGeometrySubstrateMetal.h"
#include "VROMaterialSubstrateMetal.h"
#include "VROTextureSubstrateMetal.h"

inline VROGeometrySubstrate *VRODriverMetal::newGeometrySubstrate(const VROGeometry &geometry) {
    return new VROGeometrySubstrateMetal(geometry, *this);
}

inline VROMaterialSubstrate *VRODriverMetal::newMaterialSubstrate(VROMaterial &material) {
    return new VROMaterialSubstrateMetal(material, *this);
}

inline VROTextureSubstrate *VRODriverMetal::newTextureSubstrate(VROTextureType type,
                                                              VROTextureFormat format,
                                                              VROTextureInternalFormat internalFormat,
                                                              bool sRGB,
                                                              VROMipmapMode mipmapMode,
                                                              std::vector<std::shared_ptr<VROData>> &data,
                                                              int width, int height,
                                                              std::vector<uint32_t> mipSizes,
                                                              VROWrapMode wrapS, VROWrapMode wrapT,
                                                              VROFilterMode minFilter,
                                                              VROFilterMode magFilter,
                                                              VROFilterMode mipFilter) {
    return new VROTextureSubstrateMetal(type, format, internalFormat, sRGB, mipmapMode,
                                      data, width, height, mipSizes, wrapS, wrapT,
                                      minFilter, magFilter, mipFilter, *this);
}

#endif // VRO_METAL
#endif /* VRODriverMetal_h */
