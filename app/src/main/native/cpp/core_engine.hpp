#pragma once

#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni/jni.hpp>

#include "base_renderer.hpp"
#include "opengl_renderer.hpp"
#include "vulkan_renderer.hpp"

#include "util.hpp"

namespace engine {
namespace android {

class Surface {
public:
  static constexpr auto Name() { return "android/view/Surface"; }
};

class HardwareBuffer {
public:
  static constexpr auto Name() { return "android/hardware/HardwareBuffer"; }
};

class CoreEngine {

public:
  static constexpr auto Name() { return "com/dz/camerafast/CoreEngine"; }

  static void registerNatives(JNIEnv &env) {
    jni::Class<CoreEngine>::Singleton(env);
    jni::RegisterNativePeer<CoreEngine>(
            env,
            jni::Class<CoreEngine>::Find(env),
            "peer",
            jni::MakePeer < CoreEngine, jni::jint > ,
            "initialize",
            "finalize",
            METHOD(&CoreEngine::nativeSetSurface, "nativeSetSurface"),
            METHOD(&CoreEngine::nativeSendCameraFrame, "nativeSendCameraFrame"),
            METHOD(&CoreEngine::nativeDestroy, "nativeDestroy")
    );
  }

  CoreEngine(JNIEnv &env, jni::jint renderingMode);

  CoreEngine(CoreEngine const &) = delete;

  ~CoreEngine();

  void nativeSetSurface(JNIEnv &env, jni::Object <Surface> const &surface, jni::jint width,
                        jni::jint height);

  void nativeSendCameraFrame(JNIEnv &env, jni::Object <HardwareBuffer> const &buffer, jni::jint rotationDegrees, jni::jboolean backCamera);

  void nativeDestroy(JNIEnv &env);

private:
  ANativeWindow *aNativeWindow;
  std::unique_ptr <BaseRenderer> renderer;

  AHardwareBuffer * gpuBuffer = nullptr;
};

} // namespace android
} // namespace engine
