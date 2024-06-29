#include "vulkan_renderer.hpp"

namespace engine {
namespace android {


void VulkanRenderer::doFrame(long timeStampNanos, void *data) {
  auto *renderer = reinterpret_cast<engine::android::VulkanRenderer *>(data);
  if (renderer->couldRender()) {
    renderer->render();
    // perform the check if aHwBufferQueue is not empty - then we need to catch up
    renderer->bufferQueueMutex.lock();
    if (!renderer->aHwBufferQueue.empty()) {
      LOGI("Catching up as some more buffers could be consumed!");
      // TODO buffer to Vulkan ImageView (or texture, or framebuffer, TBD)
      renderer->aHwBufferQueue.pop();
    }
    renderer->bufferQueueMutex.unlock();
  }
}

} // namespace android
} // namespace engine
