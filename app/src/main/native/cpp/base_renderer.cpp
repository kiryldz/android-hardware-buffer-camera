#include "base_renderer.hpp"

namespace engine {
namespace android {

BaseRenderer::BaseRenderer() : renderThread(std::make_unique<LooperThread>()) {
}

BaseRenderer::~BaseRenderer() {
  renderThread.reset();
}

void BaseRenderer::setWindow(ANativeWindow *window) {
  std::unique_lock <std::mutex> lock(mutex);
  aNativeWindow = window;
  // schedule an event to the render thread
  renderThread->scheduleTask([this] {
    const auto resultOk = onWindowCreated();
    if (resultOk) {
      aChoreographer = AChoreographer_getInstance();
      postChoreographerCallback();
    }
    initCondition.notify_one();
  });
  // TODO definitely could do more elegantly
  const auto str = "New Android surface arrived, waiting for " + std::string(renderingModeName()) +
                   " configuration...";
  LOGI("%s", str.c_str());
  initCondition.wait(lock);
  LOGI("New Android surface processed, resuming main thread!");
}

void BaseRenderer::updateWindowSize(int width, int height) {
  renderThread->scheduleTask([this, width, height] {
    // calculate MVP on CPU, if we would know it will be updated more often -
    // of course better move matrix calculation to GPU
    if (viewportWidth != width || viewportHeight != height) {
      viewportWidth = width;
      viewportHeight = height;
      LOGI("Update window size, width=%i, height=%i", viewportWidth, viewportHeight);
      onWindowSizeUpdated(width, height);
    }
    // update MVP in any case to cover the use-case of brining app to background and back
    updateMvp();
  });
}

void BaseRenderer::resetWindow() {
  std::unique_lock <std::mutex> lock(mutex);
  renderThread->scheduleTask([this] {
    onWindowDestroyed();
    aNativeWindow = nullptr;
    destroyCondition.notify_one();
  });
  // TODO definitely could do more elegantly
  const auto str = "Android surface destroyed, waiting until " + std::string(renderingModeName()) +
                   " will clean up...";
  LOGI("%s", str.c_str());
  destroyCondition.wait(lock);
  LOGI("Android surface destroyed, resuming main thread!");
}

void BaseRenderer::updateMvp() {
  float viewportRatio =
          static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
  float ratio = viewportRatio * bufferImageRatio;
  float fov = 45.f;
  auto proj = glm::perspective(glm::radians(fov), ratio, 0.1f, 100.0f);
  if (strcmp(this->renderingModeName(), "Vulkan") == 0) {
    // GLM was originally designed for OpenGL, where the Y coordinate of the clip coordinates is inverted.
    // The easiest way to compensate for that is to flip the sign on the scaling factor of the Y axis in the projection matrix.
    // If you don't do this, then the image will be rendered upside down.
    proj[1][1] *= -1.f;
  }
  if (backCamera) {
    proj[0][0] *= -1.f;
  }
  auto view = glm::lookAt(
          // TODO make z = f(pov) and not hardcoded 3.f
          glm::vec3(0.f, 0.f, 3.f),
          glm::vec3(0.f, 0.f, 0.f),
          // in majority of examples Y is expected to be 1.f but the actual image from camera is then flipped
          // so using Y = -1.f
          glm::vec3(0.f, -1.f, 0.f)
  );
  auto model = glm::rotate(
          glm::mat4(1.0f),
          glm::radians(static_cast<float>(rotationDegrees)),
          glm::vec3(0.0f, 0.0f, 1.0f)
          );
  mvp = proj * view * model;
  onMvpUpdated();
}

void BaseRenderer::processCameraFrame(AHardwareBuffer *aHardwareBuffer, int rotationDegrees_,
                                      bool backCamera_) {
  AHardwareBuffer_acquire(aHardwareBuffer);
  LOGI("Buffer %p acquired by %s renderer" , aHardwareBuffer, this->renderingModeName());
  renderThread->scheduleTask([aHardwareBuffer, rotationDegrees_, backCamera_, this] {
    AHardwareBuffer_Desc description;
    AHardwareBuffer_describe(aHardwareBuffer, &description);
    const auto bufferImageRatio_ =
            static_cast<float>(description.width) / static_cast<float>(description.height);
    if (bufferImageRatio_ != bufferImageRatio) {
      bufferImageRatio = bufferImageRatio_;
      updateMvp();
    }
    if (rotationDegrees_ != rotationDegrees) {
      rotationDegrees = rotationDegrees_;
      updateMvp();
    }
    if (backCamera_ != backCamera) {
      backCamera = backCamera_;
      updateMvp();
    }
    bufferMutex.lock();
    // transform HW buffer to Vulkan / OpenGL image / external texture.
    hwBufferToTexture(aHardwareBuffer);
    AHardwareBuffer_release(aHardwareBuffer);
    LOGI("Buffer %p released by %s renderer" , aHardwareBuffer, this->renderingModeName());
    bufferMutex.unlock();
    // post choreographer callback as we will need to render this texture
    postChoreographerCallback();
  });
}

} // namespace android
} // namespace engine
