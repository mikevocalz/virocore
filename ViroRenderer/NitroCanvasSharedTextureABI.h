// NitroCanvasSharedTextureABI.h
//
// Wire format for the producer -> consumer shared-texture handoff.
// Source of truth: nitro-canvas-in-Vision/cpp/NitroCanvasSharedTextureABI.h
// Vendored copy:   virocore/ViroRenderer/NitroCanvasSharedTextureABI.h
// The two files must be byte-identical. Any field change bumps
// NITRO_CANVAS_ABI_VERSION and renames the exported lookup symbol.

#ifndef NitroCanvasSharedTextureABI_h
#define NitroCanvasSharedTextureABI_h

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NITRO_CANVAS_ABI_VERSION 2u

// Bit flags for NitroCanvasSharedTextureHandle.flags.
#define NITRO_CANVAS_FLAG_SRGB 0x1u

// Fixed-width, fixed-offset on every ABI we build for (armeabi-v7a,
// arm64-v8a, x86_64, arm64 Apple): sizeof == 48, align == 8.
// 64-bit fields are used for pointers so 32-bit and 64-bit agree.
typedef struct NitroCanvasSharedTextureHandle {
    uint32_t structSize;          //  0  caller sets to sizeof(*this) before the call
    uint32_t abiVersion;          //  4  caller sets to NITRO_CANVAS_ABI_VERSION
    int32_t  width;               //  8
    int32_t  height;              // 12
    uint32_t flags;               // 16  NITRO_CANVAS_FLAG_*
    uint32_t surfaceTextureGLId;  // 20  Android Route A; 0 if unused
    uint64_t iosurface;           // 24  IOSurfaceRef as an integer; 0 if unused
    uint64_t ahardwareBuffer;     // 32  AHardwareBuffer* as an integer; 0 if unused
    int32_t  fenceFd;             // 40  Android: dup'd sync FD, ownership moves to
                                  //     the consumer. Apple: MTLSharedEvent value.
                                  //     -1 == no fence.
    uint32_t reserved0;           // 44  must be 0
} NitroCanvasSharedTextureHandle;

// Exported by libNitroCanvasInVision (.so / .dylib), resolved via dlsym.
// The symbol carries the version, so a stale peer fails to resolve and the
// consumer disables itself rather than writing a mismatched struct.
//
// Contract: the caller zero-fills `out`, sets structSize and abiVersion, and
// passes it in. The callee returns false without writing any other field if
// structSize != sizeof(NitroCanvasSharedTextureHandle) or
// abiVersion != NITRO_CANVAS_ABI_VERSION.
typedef int (*NitroCanvasLookupFn)(int32_t surfaceId,
                                   NitroCanvasSharedTextureHandle *out);

int nitro_canvas_lookup_v2(int32_t surfaceId,
                           NitroCanvasSharedTextureHandle *out);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(NitroCanvasSharedTextureHandle) == 48,
              "NitroCanvasSharedTextureHandle ABI changed without a version bump");
static_assert(offsetof(NitroCanvasSharedTextureHandle, fenceFd) == 40,
              "NitroCanvasSharedTextureHandle field order changed");
#endif

#endif /* NitroCanvasSharedTextureABI_h */
