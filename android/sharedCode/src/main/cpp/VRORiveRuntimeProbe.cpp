//
//  VRORiveRuntimeProbe.cpp
//  ViroRenderer
//
//  Link check for the statically linked rive-runtime (VIRO_WITH_RIVE). Not a
//  feature: nothing here runs, nothing calls it, and it exports no symbol.
//
//  The table below takes the address of rive::File::import and of
//  rive::gpu::RenderContextGLImpl::MakeContext. That makes the linker resolve
//  both against librive.a / librive_pls_renderer.a, so a missing archive, a
//  stale build or an ABI mismatch fails the libviro_renderer.so link instead of
//  surfacing later in a producer. `retain` keeps the table, and with it the
//  code those two entry points reach, through --gc-sections, so the .so size
//  reflects the import and GL render-context paths a Rive producer will use.
//
//  Delete this file once a real Rive producer references the library.
//
//  Built with -fno-rtti -fno-exceptions and the rive feature defines, to match
//  how the archives were compiled (see android/rive/ViroRive.cmake).
//

#include "rive/file.hpp"
#include "rive/renderer/gl/render_context_gl_impl.hpp"

namespace {

using FileImportFn = rive::rcp<rive::File> (*)(rive::Span<const uint8_t>,
                                               rive::Factory *,
                                               rive::ImportResult *,
                                               rive::FileAssetLoader *,
                                               rive::ScriptingVM *,
                                               bool);
using MakeGLContextFn = std::unique_ptr<rive::gpu::RenderContext> (*)(
    const rive::gpu::RenderContextGLImpl::ContextOptions &);

struct VRORiveRuntimeProbeTable {
    FileImportFn fileImport;
    MakeGLContextFn makeGLContext;
};

__attribute__((used, retain, visibility("hidden")))
const VRORiveRuntimeProbeTable kVRORiveRuntimeProbe = {
    &rive::File::import,
    &rive::gpu::RenderContextGLImpl::MakeContext,
};

} // namespace
