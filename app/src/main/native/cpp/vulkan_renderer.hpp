#pragma once

#include "base_renderer.hpp"
#include "vulkan_wrapper.h"

namespace engine {
namespace android {

class VulkanRenderer : public BaseRenderer {
protected:

  const char *renderingModeName() override {
    return (const char *) "Vulkan";
  }

  bool onWindowCreated() override {
    if (!InitVulkan()) {
      LOGE("Vulkan is unavailable, install vulkan and re-start");
      return false;
    }
    return true;
  }

  void onWindowSizeUpdated(int width, int height) override {
    // TODO
  }

  void onWindowDestroyed() override {
    // TODO
  }

  void hwBufferToTexture(AHardwareBuffer *buffer) override {
    // TODO
  }

  bool couldRender() const override {
    // TODO
    return false;
  }

  void render() override {
    // TODO
  }

  void postChoreographerCallback() override {
    // posting next frame callback, no need to explicitly wake the looper afterwards
    // as AChoreographer seems to operate with it's own fd and callbacks
    AChoreographer_postFrameCallback(aChoreographer, doFrame, this);
  }

private:
  ///////// Callbacks for AChoreographer and ALooper stored as private static functions

  static void doFrame(long timeStampNanos, void *data);
};
} // namespace android
} // namespace engine