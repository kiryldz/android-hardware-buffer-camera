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
      updateMvp();
      viewportWidth = width;
      viewportHeight = height;
      LOGI("Update window size, width=%i, height=%i", viewportWidth, viewportHeight);
      onWindowSizeUpdated(width, height);
    }
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

void BaseRenderer::feedHardwareBuffer(AHardwareBuffer *aHardwareBuffer) {
  AHardwareBuffer_acquire(aHardwareBuffer);
  LOGI("Buffer %p acquired" , aHardwareBuffer);
  renderThread->scheduleTask([aHardwareBuffer, this] {
    AHardwareBuffer_Desc description;
    AHardwareBuffer_describe(aHardwareBuffer, &description);
    const auto bufferImageRatio_ =
            static_cast<float>(description.width) / static_cast<float>(description.height);
    if (bufferImageRatio_ != bufferImageRatio) {
      bufferImageRatio = bufferImageRatio_;
      updateMvp();
    }
    bufferMutex.lock();
    // transform HW buffer to Vulkan / OpenGL image / external texture.
    hwBufferToTexture(aHardwareBuffer);
    AHardwareBuffer_release(aHardwareBuffer);
    LOGI("Buffer %p released" , aHardwareBuffer);
    bufferMutex.unlock();
    // post choreographer callback as we will need to render this texture
    postChoreographerCallback();
  });
}

void BaseRenderer::updateMvp() {
  float viewportRatio =
          static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
  float ratio = viewportRatio * bufferImageRatio;
  auto proj = glm::frustum(-ratio, ratio, -1.f, 1.f, 3.f, 7.f);
  auto view = glm::lookAt(
          glm::vec3(0.f, 0.f, 3.f),
          glm::vec3(0.f, 0.f, 0.f),
          glm::vec3(1.f, 0.f, 0.f)
  );
  mvp = proj * view;
  onMvpUpdated();
}

} // namespace android
} // namespace engine
