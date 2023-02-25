cmake_minimum_required(VERSION 3.10)

project(CORE_ENGINE)

if (NOT ANDROID_NDK_TOOLCHAIN_INCLUDED)
    message(FATAL_ERROR "-- Toolchain file not included, see https://developer.android.com/ndk/guides/cmake")
endif ()

add_library(
    native-engine
        SHARED
        app/src/main/native/cpp/base_renderer.cpp
        app/src/main/native/cpp/main.cpp
        app/src/main/native/cpp/core_engine.cpp
        app/src/main/native/cpp/looper_thread.cpp
        app/src/main/native/cpp/opengl_renderer.cpp
        app/src/main/native/cpp/run_loop.cpp
        app/src/main/native/cpp/vulkan_renderer.cpp
)

# Set the Android NDK path
set(ANDROID_NDK_ROOT ${CMAKE_ANDROID_NDK})
set(ANDROID_API_LEVEL ${CMAKE_ANDROID_API})
set(ANDROID_ABI ${CMAKE_ANDROID_ARCH_ABI})

# Set the glslang name - needed to build and link SPIRV
set(GLSLANG_LIBRARIES glslang)

add_subdirectory(vendor/glm)
add_subdirectory(vendor/oneTBB)
add_subdirectory(vendor/Vulkan-Headers)
add_subdirectory(vendor/volk)
add_subdirectory(vendor/glslang)

include_directories(
        vendor/oneTBB/include
        vendor/jni.hpp/include
        vendor/Vulkan-Headers/include
        vendor/glslang/glslang/Public
)

target_link_libraries(
    native-engine
    PRIVATE
        EGL
        GLESv3
        android
        log
        glm
        tbb
        volk_headers
        ${GLSLANG_LIBRARIES}
        SPIRV
)