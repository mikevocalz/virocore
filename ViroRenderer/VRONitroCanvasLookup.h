//
//  VRONitroCanvasLookup.h
//  ViroRenderer
//
//  Consumer side of the nitro-canvas-in-Vision shared-texture ABI
//  (NitroCanvasSharedTextureABI.h). Finds the producer's exported
//  nitro_canvas_lookup_v2 at runtime and calls it with the size/version
//  handshake the ABI requires.
//
//  Copyright © 2026 ViroMedia fork (mikevocalz/virocore). MIT license.
//

#ifndef VRONitroCanvasLookup_h
#define VRONitroCanvasLookup_h

#include <cstdint>
#include "NitroCanvasSharedTextureABI.h"

/*
 Outcome of one lookup call.

   Frame        The producer published a frame since the previous call for
                this surface id. `out` holds a validated handle and the caller
                owns out->fenceFd (Android: a sync FD to wait on and close;
                Apple: an MTLSharedEvent value, nothing to close).
   NoFrame      The surface exists but nothing new was published, or the id is
                unknown / disposed. The producer does not tell these apart.
   Unavailable  No producer library exports nitro_canvas_lookup_v2 (not linked,
                not loaded yet, or built against another ABI version).
   Rejected     The producer answered with a struct that fails validation. Any
                fence in it has been closed; nothing else in `out` is valid.
 */
enum class VRONitroCanvasLookupResult {
    Frame,
    NoFrame,
    Unavailable,
    Rejected,
};

/*
 Process-wide access to nitro_canvas_lookup_v2.

 Resolution is lazy. The first call tries dlsym(RTLD_DEFAULT) and then
 dlopen()s the producer library by name (libNitroCanvasInVision.so on
 Android, the NitroCanvasInVision framework image on Apple platforms). A miss
 is retried, but only on a doubling schedule of calls (1, 2, 4 ... up to every
 512th call), because the producer library is often loaded after the first
 Viro frame and a dlopen on every frame of every bound texture would cost more
 than the lookup itself. Once resolved the function pointer is kept for the
 life of the process; the library handle is never closed.

 Thread-safe. The resolved pointer is read with one atomic load.
 */
class VRONitroCanvasLookup {
public:
    /*
     Zero-fills `out`, stamps structSize and abiVersion, calls the producer,
     and validates the answer (size, version, reserved0, positive size,
     exactly one backing field set).
     */
    static VRONitroCanvasLookupResult lookup(int32_t surfaceId,
                                             NitroCanvasSharedTextureHandle *out);

    /*
     True when nitro_canvas_lookup_v2 resolves. May trigger a resolution
     attempt, subject to the retry schedule above.
     */
    static bool isAvailable();

    /*
     Replace the resolved function, for tests and for hosts that link the
     producer statically and want to skip dlsym. nullptr clears the override
     and the cached resolution, so the next call resolves from scratch.
     */
    static void setLookupFunctionForTesting(NitroCanvasLookupFn fn);

    /*
     Validation used by lookup(), exposed for tests. Checks everything the
     ABI lets a consumer check without touching the GPU object.
     */
    static bool isValidHandle(const NitroCanvasSharedTextureHandle &handle);

    // The exported symbol name; carries the ABI version.
    static const char *symbolName() { return "nitro_canvas_lookup_v2"; }
};

#endif /* VRONitroCanvasLookup_h */
