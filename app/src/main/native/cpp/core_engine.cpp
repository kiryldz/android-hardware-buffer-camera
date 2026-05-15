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
  std::lock_guard<std::mutex> lock(coreEngineMutex);
  if (surface.get() != nullptr) {
    // ANativeWindow_fromSurface returns a NEW strong reference (+1 on the refcount); we own it
    // and must release it ourselves. Do NOT also call ANativeWindow_acquire — that would leak.
    auto *nativeWindow = ANativeWindow_fromSurface(&env, jni::Unwrap(*surface.get()));
    if (nativeWindow == nullptr) {
      // fromSurface can fail (e.g. surface already invalidated). Bail before driving any
      // render-thread size work without a valid native window.
      LOGE("ANativeWindow_fromSurface returned null; skipping surface configuration");
      return;
    }
    if (nativeWindow != aNativeWindow) {
      if (aNativeWindow != nullptr) {
        ANativeWindow_release(aNativeWindow);
      }
      aNativeWindow = nativeWindow;
      if (renderer) {
        renderer->setWindow(nativeWindow);
      }
    } else {
      // Same underlying window — drop the duplicate ref returned by fromSurface.
      ANativeWindow_release(nativeWindow);
    }
    if (renderer) {
      renderer->updateWindowSize(width, height);
    }
  } else {
    if (renderer) {
      renderer->resetWindow();
    }
    if (aNativeWindow != nullptr) {
      ANativeWindow_release(aNativeWindow);
      aNativeWindow = nullptr;
    }
  }
}

/** called from Android main thread on every TextureView size tick **/
void CoreEngine::nativeUpdateWindowSize(JNIEnv &env, jni::jint width, jni::jint height) {
  std::lock_guard<std::mutex> lock(coreEngineMutex);
  if (renderer && aNativeWindow != nullptr) {
    renderer->updateWindowSize(width, height);
  }
}

/** called from worker thread **/
void CoreEngine::nativeSendCameraFrame(JNIEnv &env, const jni::Object<HardwareBuffer> &buffer,
                                       jni::jint rotationDegrees, jni::jboolean backCamera) {
  std::lock_guard<std::mutex> lock(coreEngineMutex);
  if (!renderer) {
    return;
  }
  auto cameraBuffer = AHardwareBuffer_fromHardwareBuffer(&env, jni::Unwrap(*buffer.get()));
  AHardwareBuffer_Desc cameraBufferDescription;
  AHardwareBuffer_describe(cameraBuffer, &cameraBufferDescription);
  if (cameraBufferDescription.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE) {
    renderer->processCameraFrame(cameraBuffer, rotationDegrees, backCamera);
  } else {
    AHardwareBuffer_Desc gpuBufferDescription {
            .width = cameraBufferDescription.width,
            .height = cameraBufferDescription.height,
            .layers = cameraBufferDescription.layers,
            .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
            .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER,
    };
    if (!gpuBuffer) {
      int res = AHardwareBuffer_allocate(&gpuBufferDescription, &gpuBuffer);
      LOGI("HW buffer from camera does not support AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE.");
      LOGI("Allocating GPU HW buffer manually. Result: %d", res);
    }
    void* gpuData = nullptr;
    void* cpuData = nullptr;
    AHardwareBuffer_lock(cameraBuffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &cpuData);
    AHardwareBuffer_lock(gpuBuffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &gpuData);
    memcpy(gpuData, cpuData, cameraBufferDescription.height * cameraBufferDescription.width * 4);
    AHardwareBuffer_unlock(cameraBuffer, nullptr);
    AHardwareBuffer_unlock(gpuBuffer, nullptr);
    renderer->processCameraFrame(gpuBuffer, rotationDegrees, backCamera);
  }
}

void CoreEngine::nativeRefit(JNIEnv &env) {
  std::lock_guard<std::mutex> lock(coreEngineMutex);
  if (renderer) {
    renderer->refit();
  }
}

void CoreEngine::nativeDestroy(JNIEnv &env) {
  std::lock_guard<std::mutex> lock(coreEngineMutex);
  LOGI("Core engine destroy started");

  if (renderer && aNativeWindow != nullptr) {
    renderer->resetWindow();
  }

  renderer.reset();

  if (aNativeWindow != nullptr) {
    ANativeWindow_release(aNativeWindow);
    aNativeWindow = nullptr;
  }

  if (gpuBuffer != nullptr) {
    AHardwareBuffer_release(gpuBuffer);
    gpuBuffer = nullptr;
  }

  LOGI("Core engine destroy passed");
}

} // namespace android
} // namespace engine