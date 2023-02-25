#include "base_renderer.hpp"

namespace engine {
namespace android {

BaseRenderer::BaseRenderer(): renderThread(std::make_unique<LooperThread>()) {
}

BaseRenderer::~BaseRenderer() {
  renderThread.reset();
}

void BaseRenderer::setWindow(ANativeWindow *window) {
  std::unique_lock<std::mutex> lock(mutex);
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
  const auto str = "New Android surface arrived, waiting for " + std::string(renderingModeName()) + " configuration...";
  LOGI("%s", str.c_str());
  initCondition.wait(lock);
  LOGI("New Android surface processed, resuming main thread!");
}

void BaseRenderer::updateWindowSize(int width, int height) {
  // schedule an event to the render thread
  renderThread->scheduleTask([this, width, height] {
      viewportWidth = width;
      viewportHeight = height;
      LOGI("Update window size, width=%i, height=%i", viewportWidth, viewportHeight);
      onWindowSizeUpdated(width, height);
  });
}

void BaseRenderer::resetWindow() {
  std::unique_lock<std::mutex> lock(mutex);
  renderThread->scheduleTask([this] {
      onWindowDestroyed();
      aNativeWindow = nullptr;
      destroyCondition.notify_one();
  });
  // TODO definitely could do more elegantly
  const auto str = "Android surface destroyed, waiting until " + std::string(renderingModeName()) + " will clean up...";
  LOGI("%s", str.c_str());
  destroyCondition.wait(lock);
  LOGI("Android surface destroyed, resuming main thread!");
}

void BaseRenderer::feedHardwareBuffer(AHardwareBuffer *aHardwareBuffer) {
  if (!hardwareBufferDescribed) {
    AHardwareBuffer_Desc description;
    AHardwareBuffer_describe(aHardwareBuffer, &description);
    bufferImageRatio = static_cast<float>(description.width) / static_cast<float>(description.height);
  }
  // it seems that there's no leak even if we do not explicitly acquire / release but guess better do that
  AHardwareBuffer_acquire(aHardwareBuffer);
  aHwBufferQueue.push(aHardwareBuffer);
//  LOGI("Feed new hardware buffer, size %u", aHwBufferQueue.unsafe_size());
  renderThread->scheduleTask([this] {
      AHardwareBuffer * aHardwareBuffer;
      if (aHwBufferQueue.try_pop(aHardwareBuffer)) {
        hwBufferToTexture(aHardwareBuffer);
        // post choreographer callback as we will need to render this texture
        postChoreographerCallback();
      }
  });
}

} // namespace android
} // namespace engine
