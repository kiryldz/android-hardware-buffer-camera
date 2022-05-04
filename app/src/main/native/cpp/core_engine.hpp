#pragma once

#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni/jni.hpp>

#include "opengl_renderer.hpp"
#include "util.hpp"

namespace engine {
namespace android {

class Surface
{
public:
  static constexpr auto Name() { return "android/view/Surface"; }
};

class HardwareBuffer
{
public:
  static constexpr auto Name() { return "android/hardware/HardwareBuffer"; }
};

class CoreEngine {

public:
  static constexpr auto Name() { return "com/dz/camerafast/CoreEngine"; }

  static void registerNatives(JNIEnv& env) {
    jni::Class<CoreEngine>::Singleton(env);
    jni::RegisterNativePeer<CoreEngine>(
      env,
      jni::Class<CoreEngine>::Find(env),
      "peer",
      jni::MakePeer<CoreEngine>,
      "initialize",
      "finalize",
      METHOD(&CoreEngine::nativeSetSurface, "nativeSetSurface"),
      METHOD(&CoreEngine::nativeFeedHardwareBuffer, "nativeFeedHardwareBuffer"),
      METHOD(&CoreEngine::nativeDestroy, "nativeDestroy")
    );
  }

  CoreEngine(JNIEnv & env);
  CoreEngine(CoreEngine const &) = delete;
  ~CoreEngine();

  void nativeSetSurface(JNIEnv & env, jni::Object<Surface> const & surface, jni::jint width, jni::jint height);

  void nativeFeedHardwareBuffer(JNIEnv & env, jni::Object<HardwareBuffer> const & buffer);

  void nativeDestroy(JNIEnv & env);

private:
  ANativeWindow * aNativeWindow;
  std::unique_ptr<OpenGLRenderer> openGlRenderer;

};

} // namespace android
} // namespace engine
