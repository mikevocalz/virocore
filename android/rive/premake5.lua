-- Premake entry for the rive-runtime static libraries that link into libviro_renderer.so.
-- Run it through scripts/build-rive-runtime.sh, which calls rive-runtime's own build_rive.sh
-- from this directory. Outputs land in android/rive/out/ and fetched sources in
-- android/rive/dependencies/; both are gitignored.
--
-- The vendored tree in ViroRenderer/rive-runtime stays as upstream ships it, apart from the
-- local changes listed in ViroRenderer/rive-runtime/NOTICE.md. Everything ViroCore needs on
-- top of upstream lives in this file.

local RIVE_RUNTIME = path.getabsolute('../../ViroRenderer/rive-runtime')

dofile(RIVE_RUNTIME .. '/build/rive_build_config.lua')

-- Hidden visibility for every rive project. libviro_renderer.so also links with
-- -Wl,--exclude-libs on these archives, so no rive symbol leaves the .so and nothing collides
-- with librive-android.so when an app ships @rive-app/react-native as well.
buildoptions({ '-fvisibility=hidden', '-fvisibility-inlines-hidden' })

-- Same project set rive-runtime's renderer/premake5.lua builds, minus the path_fiddle sample,
-- the GLFW download it needs, and the image decoders (built with --no-rive-decoders).
dofile(RIVE_RUNTIME .. '/renderer/premake5_pls_renderer.lua')
dofile(RIVE_RUNTIME .. '/premake5_v2.lua')
