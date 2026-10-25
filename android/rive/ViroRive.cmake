# rive-runtime static libraries for libviro_renderer.so (VIRO_WITH_RIVE).
#
# Included from android/sharedCode/CMakeLists.txt after viro_renderer is defined.
# The archives come from scripts/build-rive-runtime.sh (rive-runtime's own premake build).
# When they are missing or were built from another pin, configure runs that script once.
#
# Exposes:
#   VIRO_RIVE_LINKED          ON when the archives are linked into viro_renderer
#   viro_rive_sources(<src>)  compiles sources that include rive headers with the flags and
#                             defines the archives were built with
#
# Only arm64-v8a is built. Other ABIs compile without Rive and VRO_HAS_RIVE_RUNTIME=0.

set(VIRO_RIVE_DIR      ${CMAKE_CURRENT_LIST_DIR})
set(VIRO_RIVE_RUNTIME  ${CMAKE_CURRENT_LIST_DIR}/../../ViroRenderer/rive-runtime)
set(VIRO_RIVE_OUT      ${VIRO_RIVE_DIR}/out/android_arm64_release)
set(VIRO_RIVE_LIBS     rive_pls_renderer rive rive_harfbuzz rive_sheenbidi rive_yoga)
set(VIRO_RIVE_LINKED   OFF)

# Defines the archives were compiled with. Headers change class layout on DEBUG/RELEASE
# (RIVE_DEBUG_CODE members), so every TU that includes rive headers must use the same set,
# whatever the Gradle build type. Keep in sync with RIVE_PREMAKE_ARGS in the build script.
set(VIRO_RIVE_COMPILE_DEFINITIONS
    RIVE_ANDROID WITH_RIVE_TEXT WITH_RIVE_LAYOUT RELEASE NDEBUG YOGA_EXPORT=)

# Public rive headers include yoga, which premake fetches into android/rive/dependencies at the
# ref rive-runtime pins in dependencies/yoga.ref.
file(READ ${VIRO_RIVE_RUNTIME}/dependencies/yoga.ref VIRO_RIVE_YOGA_REF)
string(STRIP "${VIRO_RIVE_YOGA_REF}" VIRO_RIVE_YOGA_REF)
set(VIRO_RIVE_INCLUDE_DIRS
    ${VIRO_RIVE_RUNTIME}/include
    ${VIRO_RIVE_RUNTIME}/renderer/include
    ${VIRO_RIVE_RUNTIME}/dependencies
    ${VIRO_RIVE_DIR}/dependencies/rive-app_yoga_${VIRO_RIVE_YOGA_REF})

function(viro_rive_sources)
    set_source_files_properties(${ARGN} PROPERTIES
        COMPILE_DEFINITIONS "${VIRO_RIVE_COMPILE_DEFINITIONS}"
        INCLUDE_DIRECTORIES "${VIRO_RIVE_INCLUDE_DIRS}"
        # rive is built as C++17 without RTTI or exceptions, with yoga's and harfbuzz's symbols
        # renamed by force-included headers. Match all of it so types mangle the same way.
        COMPILE_OPTIONS
            "-std=c++17;-fno-rtti;-fno-exceptions;-UDEBUG;--include=rive_yoga_renames.h;--include=rive_harfbuzz_renames.h")
endfunction()

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(STATUS "rive-runtime: skipped for ${ANDROID_ABI} (arm64-v8a only)")
    target_compile_definitions(viro_renderer PRIVATE VRO_HAS_RIVE_RUNTIME=0)
    return()
endif()

file(READ ${VIRO_RIVE_DIR}/RIVE_RUNTIME_COMMIT VIRO_RIVE_COMMIT)
string(STRIP "${VIRO_RIVE_COMMIT}" VIRO_RIVE_COMMIT)

set(VIRO_RIVE_STAMP ${VIRO_RIVE_OUT}/viro_rive.stamp)
set(VIRO_RIVE_STAMP_OK OFF)
if(EXISTS ${VIRO_RIVE_STAMP})
    file(READ ${VIRO_RIVE_STAMP} VIRO_RIVE_STAMP_TEXT)
    string(FIND "${VIRO_RIVE_STAMP_TEXT}" "${VIRO_RIVE_COMMIT} " VIRO_RIVE_STAMP_POS)
    if(VIRO_RIVE_STAMP_POS EQUAL 0)
        set(VIRO_RIVE_STAMP_OK ON)
    endif()
endif()
foreach(lib ${VIRO_RIVE_LIBS})
    if(NOT EXISTS ${VIRO_RIVE_OUT}/lib${lib}.a)
        set(VIRO_RIVE_STAMP_OK OFF)
    endif()
endforeach()

if(NOT VIRO_RIVE_STAMP_OK)
    message(STATUS "rive-runtime: archives missing or stale, running scripts/build-rive-runtime.sh")
    execute_process(
        COMMAND ${VIRO_RIVE_DIR}/../../scripts/build-rive-runtime.sh clean
        RESULT_VARIABLE VIRO_RIVE_BUILD_RESULT
        OUTPUT_VARIABLE VIRO_RIVE_BUILD_LOG
        ERROR_VARIABLE  VIRO_RIVE_BUILD_LOG)
    if(NOT VIRO_RIVE_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR
            "rive-runtime build failed (exit ${VIRO_RIVE_BUILD_RESULT}). Run "
            "scripts/build-rive-runtime.sh by hand, or configure with -DVIRO_WITH_RIVE=OFF.\n"
            "${VIRO_RIVE_BUILD_LOG}")
    endif()
endif()

foreach(lib ${VIRO_RIVE_LIBS})
    add_library(viro_${lib} STATIC IMPORTED)
    set_target_properties(viro_${lib} PROPERTIES IMPORTED_LOCATION ${VIRO_RIVE_OUT}/lib${lib}.a)
    list(APPEND VIRO_RIVE_IMPORTED viro_${lib})
    list(APPEND VIRO_RIVE_EXCLUDE lib${lib}.a)
endforeach()
string(REPLACE ";" "," VIRO_RIVE_EXCLUDE "${VIRO_RIVE_EXCLUDE}")

# Link check only: see the comment at the top of VRORiveRuntimeProbe.cpp. No runtime behaviour.
target_sources(viro_renderer PRIVATE ${VIRO_ANDROID_SRC}/VRORiveRuntimeProbe.cpp)
viro_rive_sources(${VIRO_ANDROID_SRC}/VRORiveRuntimeProbe.cpp)

target_link_libraries(viro_renderer
    ${VIRO_RIVE_IMPORTED}
    # The archives are built with -fvisibility=hidden. --exclude-libs also hides anything that
    # still carries default visibility, so librive-android.so in the same app cannot bind to
    # ViroCore's copy or the other way round.
    "-Wl,--exclude-libs,${VIRO_RIVE_EXCLUDE}")
target_compile_definitions(viro_renderer PRIVATE VRO_HAS_RIVE_RUNTIME=1)
set(VIRO_RIVE_LINKED ON)
message(STATUS "rive-runtime ${VIRO_RIVE_COMMIT}: linked statically (${VIRO_RIVE_OUT})")
