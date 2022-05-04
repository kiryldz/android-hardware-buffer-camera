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
include_directories(vendor/jni.hpp/include)

target_link_libraries(
    native-engine
    PRIVATE
        EGL
        # we are using GLES2 as glEGLImageTargetTexture2DOES is missing in GLES3 while we need external texture rendering
        # need to investigate more, link to start https://www.khronos.org/registry/OpenGL/extensions/OES/OES_EGL_image_external.txt
        # perhaps we could still use GLES3 with ext file from either GLES or GLES2 but for confidence using GLES2 now
        GLESv2
        android
        log
        glm
)