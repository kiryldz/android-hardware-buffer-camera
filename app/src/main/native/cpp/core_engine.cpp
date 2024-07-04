#include "core_engine.hpp"

namespace engine {
namespace android {

CoreEngine::CoreEngine(JNIEnv &env, jni::jint renderingMode) : aNativeWindow(nullptr) {
  switch (renderingMode) {
    case 0: {
      LOGI("Using OpenGL ES renderer");
      renderer = std::make_unique<OpenGLRenderer>();
      break;
    }
    case 1: {
      LOGI("Using Vulkan renderer");
      renderer = std::make_unique<VulkanRenderer>();
      break;
    }
    default: {
      throw std::exception();
    }
  }
}

CoreEngine::~CoreEngine() = default;

/** called from Android main thread **/
void CoreEngine::nativeSetSurface(JNIEnv &env, const jni::Object<Surface> &surface,
                                  jni::jint width, jni::jint height) {
  if (surface.get() != nullptr) {
    auto *nativeWindow = ANativeWindow_fromSurface(&env, jni::Unwrap(*surface.get()));
    if (nativeWindow != aNativeWindow) {
      aNativeWindow = nativeWindow;
      ANativeWindow_acquire(aNativeWindow);
      renderer->setWindow(nativeWindow);
    }
    if (nativeWindow) {
      renderer->updateWindowSize(width, height);
    }
  } else {
    renderer->resetWindow();
    ANativeWindow_release(aNativeWindow);
    aNativeWindow = nullptr;
  }
}

/** called from worker thread **/
void CoreEngine::nativeFeedHardwareBuffer(JNIEnv &env,
                                          const jni::Object<HardwareBuffer> &buffer) {
  auto cpuBuffer = AHardwareBuffer_fromHardwareBuffer(&env, jni::Unwrap(*buffer.get()));
  AHardwareBuffer_Desc cpuDescription;
  AHardwareBuffer_describe(cpuBuffer, &cpuDescription);
  AHardwareBuffer_Desc gpuDescription {
    .width = cpuDescription.width,
    .height = cpuDescription.height,
//    .height = 1,
    .layers = cpuDescription.layers,
    .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
    .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER,
//    .usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER,
  };
  AHardwareBuffer *gpuBuffer = nullptr;
//  LOGI("Supported: %i", AHardwareBuffer_isSupported(&gpuDescription));
  int result = AHardwareBuffer_allocate(&gpuDescription, &gpuBuffer);
  LOGI("Results for allocate: %d", result);
  void* gpuData = nullptr;
  void* cpuData = nullptr;
  auto resCpu = AHardwareBuffer_lock(cpuBuffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &cpuData);
  auto resGpu = AHardwareBuffer_lock(gpuBuffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &gpuData);
  LOGI("Results for locks: %d and %d", resCpu, resGpu);
  memcpy(gpuData, cpuData, cpuDescription.height * cpuDescription.width * 4);
  AHardwareBuffer_unlock(cpuBuffer, nullptr);
  AHardwareBuffer_unlock(gpuBuffer, nullptr);
  renderer->feedHardwareBuffer(gpuBuffer);
}

void CoreEngine::nativeDestroy(JNIEnv &env) {
  LOGI("Core engine destroy started");
  renderer.reset();
  LOGI("Core engine destroy passed");
}

} // namespace android
} // namespace engine