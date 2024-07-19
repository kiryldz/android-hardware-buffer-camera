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
  auto cameraBuffer = AHardwareBuffer_fromHardwareBuffer(&env, jni::Unwrap(*buffer.get()));
  AHardwareBuffer_Desc cameraBufferDescription;
  AHardwareBuffer_describe(cameraBuffer, &cameraBufferDescription);
  if (cameraBufferDescription.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE) {
    renderer->feedHardwareBuffer(cameraBuffer);
  } else {
    AHardwareBuffer_Desc gpuBufferDescription {
            .width = cameraBufferDescription.width,
            .height = cameraBufferDescription.height,
            .layers = cameraBufferDescription.layers,
            .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
            .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER,
    };
    static int result = AHardwareBuffer_allocate(&gpuBufferDescription, &gpuBuffer);
    void* gpuData = nullptr;
    void* cpuData = nullptr;
    AHardwareBuffer_lock(cameraBuffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &cpuData);
    AHardwareBuffer_lock(gpuBuffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &gpuData);
    memcpy(gpuData, cpuData, cameraBufferDescription.height * cameraBufferDescription.width * 4);
    AHardwareBuffer_unlock(cameraBuffer, nullptr);
    AHardwareBuffer_unlock(gpuBuffer, nullptr);
    renderer->feedHardwareBuffer(gpuBuffer);
  }
}

void CoreEngine::nativeDestroy(JNIEnv &env) {
  LOGI("Core engine destroy started");
  renderer.reset();
  LOGI("Core engine destroy passed");
}

} // namespace android
} // namespace engine