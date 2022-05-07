cmake_minimum_required(VERSION 3.10)

project(CORE_ENGINE)

if (NOT ANDROID_NDK_TOOLCHAIN_INCLUDED)
    message(FATAL_ERROR "-- Toolchain file not included, see https://developer.android.com/ndk/guides/cmake")
endif ()

add_library(
    native-engine
        SHARED
        app/src/main/native/cpp/main.cpp
        app/src/main/native/cpp/core_engine.cpp
        app/src/main/native/cpp/opengl_renderer.cpp
)

add_subdirectory(vendor/glm)
add_subdirectory(vendor/oneTBB)

include_directories(
        vendor/oneTBB/include
        vendor/jni.hpp/include
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
)