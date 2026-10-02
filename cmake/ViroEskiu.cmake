# ViroEskiu.cmake
#
# Reusable, opt-in Eskiu object compiler helper.
# Nothing includes this file by default; production targets adopt it explicitly
# after their per-module architecture/benchmark gate.

include_guard(GLOBAL)
include(CMakeParseArguments)

set(VIRO_ESKIU_EXPECTED_VERSION "0.9.2" CACHE STRING "Pinned Eskiu compiler version")
find_program(VIRO_ESKIU_COMPILER NAMES eskiuc)

function(viro_eskiu_require_compiler)
    if(NOT VIRO_ESKIU_COMPILER)
        message(FATAL_ERROR
            "eskiuc was not found. Install the pinned Eskiu v${VIRO_ESKIU_EXPECTED_VERSION} "
            "toolchain or set VIRO_ESKIU_COMPILER.")
    endif()

    execute_process(
        COMMAND "${VIRO_ESKIU_COMPILER}" --version
        OUTPUT_VARIABLE _eskiu_version_stdout
        ERROR_VARIABLE _eskiu_version_stderr
        RESULT_VARIABLE _eskiu_version_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    set(_eskiu_version "${_eskiu_version_stdout}${_eskiu_version_stderr}")
    if(NOT _eskiu_version_result EQUAL 0)
        message(FATAL_ERROR "Failed to run eskiuc --version: ${_eskiu_version}")
    endif()
    string(REGEX MATCH "^Eskiu +([0-9]+\\.[0-9]+\\.[0-9]+)( .*)?$" _eskiu_version_match "${_eskiu_version}")
    set(_eskiu_version_token "${CMAKE_MATCH_1}")
    if(_eskiu_version_token STREQUAL "" OR
       NOT _eskiu_version_token STREQUAL VIRO_ESKIU_EXPECTED_VERSION)
        message(FATAL_ERROR
            "ViroCore expects exactly Eskiu ${VIRO_ESKIU_EXPECTED_VERSION}; found: ${_eskiu_version}")
    endif()
endfunction()

# viro_eskiu_compile_object(
#   OUT_VAR
#   SOURCE <file.esk>
#   [NAME <stem>]
#   [TARGET <llvm-triple>]
#   [MCPU <cpu>]
#   [MATTR <features>]
#   [RELOC <model>]
#   [OPT <0|1|2|3>]
#   [FREESTANDING]
#   [SAFE]
# )
#
# Returns an absolute generated object path in OUT_VAR. The caller may include
# that object in a C/C++ target's source list.
function(viro_eskiu_compile_object OUT_VAR)
    set(options FREESTANDING SAFE)
    set(oneValueArgs SOURCE NAME TARGET MCPU MATTR RELOC OPT)
    cmake_parse_arguments(VE "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT VE_SOURCE)
        message(FATAL_ERROR "viro_eskiu_compile_object requires SOURCE")
    endif()

    viro_eskiu_require_compiler()

    get_filename_component(_source "${VE_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(_stem "${_source}" NAME_WE)
    if(VE_NAME)
        set(_stem "${VE_NAME}")
    endif()

    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/eskiu")
    set(_object "${_out_dir}/${_stem}.o")
    set(_args "${_source}" -c -o "${_object}")

    if(VE_TARGET)
        list(APPEND _args --target "${VE_TARGET}")
    endif()
    if(VE_MCPU)
        list(APPEND _args --mcpu "${VE_MCPU}")
    endif()
    if(VE_MATTR)
        list(APPEND _args --mattr "${VE_MATTR}")
    endif()
    if(VE_RELOC)
        list(APPEND _args --reloc "${VE_RELOC}")
    endif()
    if(DEFINED VE_OPT AND NOT VE_OPT STREQUAL "")
        if(NOT VE_OPT MATCHES "^[0-3]$")
            message(FATAL_ERROR "OPT must be 0, 1, 2, or 3")
        endif()
        list(APPEND _args "-O${VE_OPT}")
    endif()
    if(VE_FREESTANDING)
        list(APPEND _args --freestanding)
    endif()
    if(VE_SAFE)
        list(APPEND _args --safe)
    endif()

    add_custom_command(
        OUTPUT "${_object}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out_dir}"
        COMMAND "${VIRO_ESKIU_COMPILER}" ${_args}
        DEPENDS "${_source}"
        COMMENT "Compiling Eskiu object ${_stem}.o"
        VERBATIM
    )

    set_source_files_properties("${_object}" PROPERTIES
        GENERATED TRUE
        EXTERNAL_OBJECT TRUE
    )
    set(${OUT_VAR} "${_object}" PARENT_SCOPE)
endfunction()
