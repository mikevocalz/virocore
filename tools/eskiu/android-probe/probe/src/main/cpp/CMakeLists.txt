cmake_minimum_required(VERSION 3.22.1)
project(viro_eskiu_android_probe LANGUAGES C CXX)

if(NOT VIRO_ESKIU_COMPILER)
    message(FATAL_ERROR "VIRO_ESKIU_COMPILER is required")
endif()

get_filename_component(
    VIRO_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../../.."
    ABSOLUTE)

set(ESKIU_SOURCE "${VIRO_REPO_ROOT}/tests/eskiu/android_target_probe.esk")
set(ESKIU_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/android_target_probe.o")

add_custom_command(
    OUTPUT "${ESKIU_OBJECT}"
    COMMAND "${VIRO_ESKIU_COMPILER}"
            "${ESKIU_SOURCE}"
            -c
            -o "${ESKIU_OBJECT}"
            --target aarch64-linux-android
    DEPENDS "${ESKIU_SOURCE}"
    COMMENT "Compiling Eskiu Android probe object"
    VERBATIM)

set_source_files_properties(
    "${ESKIU_OBJECT}"
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)

add_library(
    viro_eskiu_probe
    SHARED
    jni_probe.cpp
    "${ESKIU_OBJECT}")

target_compile_features(viro_eskiu_probe PRIVATE cxx_std_17)
target_link_options(
    viro_eskiu_probe
    PRIVATE
    "-Wl,-z,max-page-size=16384")
