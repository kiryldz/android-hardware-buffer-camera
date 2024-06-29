cmake_minimum_required(VERSION 3.10)

project(CORE_ENGINE)

if (NOT ANDROID_NDK_TOOLCHAIN_INCLUDED)
    message(FATAL_ERROR "-- Toolchain file not included, see https://developer.android.com/ndk/guides/cmake")
endif ()

add_library(
    native-engine
        SHARED
        app/src/main/native/cpp/main.cpp
        app/src/main/native/cpp/base_renderer.cpp
        app/src/main/native/cpp/core_engine.cpp
        app/src/main/native/cpp/opengl_renderer.cpp
        app/src/main/native/cpp/vulkan_renderer.cpp
        app/src/main/native/cpp/vulkan_wrapper.cpp
        app/src/main/native/cpp/looper_thread.cpp
        app/src/main/native/cpp/run_loop.cpp
)

add_subdirectory(vendor/glm)

include_directories(
        vendor/jni.hpp/include
        libs/shaderc/include
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
    -DVK_USE_PLATFORM_ANDROID_KHR") # needed to enable Vulkan extension to work with Android native window

add_library(shaderc STATIC IMPORTED)
set_target_properties(shaderc PROPERTIES IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/shaderc/${ANDROID_STL}/${ANDROID_ABI}/libshaderc.a)

target_link_libraries(
    native-engine
    PRIVATE
        EGL
        GLESv3
        android
        log
        glm
)