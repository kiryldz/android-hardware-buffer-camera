#include "core_engine.hpp"

namespace engine {
namespace android {

CoreEngine::CoreEngine(JNIEnv & env): aNativeWindow(nullptr), openGlRenderer(std::make_unique<OpenGLRenderer>()) {
  LOGI("Core engine created");
}

CoreEngine::~CoreEngine() = default;

/** called from Android main thread **/
void CoreEngine::nativeSetSurface(JNIEnv & env, const jni::Object <Surface> & surface, jni::jint width, jni::jint height) {
  if (surface.get() != nullptr) {
    auto * nativeWindow = ANativeWindow_fromSurface(&env, jni::Unwrap(*surface.get()));
    if (nativeWindow != aNativeWindow) {
      aNativeWindow = nativeWindow;
      ANativeWindow_acquire(aNativeWindow);
      openGlRenderer->setWindow(nativeWindow);
    }
    if (nativeWindow) {
      openGlRenderer->updateWindowSize(width, height);
    }
  } else {
    openGlRenderer->resetWindow();
    ANativeWindow_release(aNativeWindow);
    aNativeWindow = nullptr;
  }
}

/** called from worker thread **/
void CoreEngine::nativeFeedHardwareBuffer(JNIEnv & env, const jni::Object <HardwareBuffer> & buffer) {
  openGlRenderer->feedHardwareBuffer(AHardwareBuffer_fromHardwareBuffer(&env, jni::Unwrap(*buffer.get())));
}

void CoreEngine::nativeDestroy(JNIEnv & env) {
  LOGI("Core engine destroy started");
  openGlRenderer.reset();
  LOGI("Core engine destroy passed");
}

} // namespace android
} // namespace engine