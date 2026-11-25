# Host-side unit tests for header-only renderer logic. Not part of the Android
# build; configure and run from any desktop toolchain:
#
#   cmake -S android/sharedCode/src/test/cpp -B /tmp/viro-host-tests
#   cmake --build /tmp/viro-host-tests && ctest --test-dir /tmp/viro-host-tests
#
# tools/run-host-tests.sh does the same without CMake.
cmake_minimum_required(VERSION 3.13)
project(ViroHostTests CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VIRO_ANDROID_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp)

enable_testing()

function(viro_host_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${VIRO_ANDROID_SRC}
        ${VIRO_ANDROID_SRC}/include)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

viro_host_test(VROHandPinchStateTest)
viro_host_test(VROOpenXRSimpleControllerBindingsTest)
