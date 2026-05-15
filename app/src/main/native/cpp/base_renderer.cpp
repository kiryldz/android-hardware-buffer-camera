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
  bool needsSchedule;
  {
    std::lock_guard<std::mutex> lock(resizeMutex);
    pendingViewportWidth = width;
    pendingViewportHeight = height;
    needsSchedule = !resizeTaskScheduled;
    resizeTaskScheduled = true;
  }
  if (!needsSchedule) {
    return;
  }
  renderThread->scheduleTask([this] {
    // Keep resizeTaskScheduled = true for the whole duration of this task so producers running
    // in parallel can't schedule a second task — they'll instead just update pending dims, which
    // we'll pick up on the next iteration of this loop. The slot is released only after we
    // observe a tick where pending == applied, guaranteeing no producer's latest size is lost.
    while (true) {
      int width;
      int height;
      bool sizeChanged;
      {
        // Single critical section that covers the read of pending dims, the compare against
        // applied dims, and (when we proceed) the write of applied dims. Keeping the compare
        // and the write under the same lock as the producer's pending-store means we can't
        // straddle a producer update — either the producer's new pending lands BEFORE our
        // compare (we apply it this iteration) or AFTER our scheduled-flag clear (it schedules
        // a fresh task). The heavy work below runs unlocked so producers stay responsive.
        std::lock_guard<std::mutex> lock(resizeMutex);
        width = pendingViewportWidth;
        height = pendingViewportHeight;
        sizeChanged = (viewportWidth != width || viewportHeight != height);
        if (!sizeChanged) {
          resizeTaskScheduled = false;
        } else {
          viewportWidth = width;
          viewportHeight = height;
        }
      }
      if (!sizeChanged) {
        // Refresh MVP on the way out even when the size was unchanged: producers (e.g. surface
        // re-creation on background → foreground) can re-fire updateWindowSize with the same
        // dimensions, and the rest of the renderer relies on the MVP being current.
        updateMvp();
        return;
      }
      LOGI("Update window size, width=%i, height=%i", width, height);
      // Per-tick lightweight hook — OpenGL's glViewport must run on every layout tick or the
      // aspect ratio of rendered content goes wrong during a resize. Heavy size-driven work
      // (Vulkan swapchain rebuild) is gated by onRefit() instead.
      onWindowSizeUpdated(width, height);
    }
  });
}

void BaseRenderer::refit() {
  renderThread->scheduleTask([this] {
    if (viewportWidth > 0 && viewportHeight > 0) {
      onRefit(viewportWidth, viewportHeight);
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

void BaseRenderer::updateMvp() {
  if (viewportWidth <= 0 || viewportHeight <= 0) {
    return;
  }
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
