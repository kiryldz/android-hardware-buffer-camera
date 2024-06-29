#pragma once

#include <cassert>

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

    VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "tutorial05_triangle_window",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "tutorial",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_MAKE_VERSION(1, 0, 0),
    };

    // create a device
    CreateVulkanDevice(&appInfo);
    CreateSwapChain();

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
  ///////// Structs and variables

  struct VulkanDeviceInfo {
    bool initialized_;

    VkInstance instance_;
    VkPhysicalDevice gpuDevice_;
    VkPhysicalDeviceMemoryProperties gpuMemoryProperties_;
    VkDevice device_;
    uint32_t queueFamilyIndex_;

    VkSurfaceKHR surface_;
    VkQueue queue_;
  };
  VulkanDeviceInfo device;

  struct VulkanSwapchainInfo {
    VkSwapchainKHR swapchain_;
    uint32_t swapchainLength_;

    VkExtent2D displaySize_;
    VkFormat displayFormat_;

    // array of frame buffers and views
    VkFramebuffer* framebuffers_;
    VkImage* displayImages_;
    VkImageView* displayViews_;
  };
  VulkanSwapchainInfo swapchain;

  typedef struct texture_object {
    VkSampler sampler;
    VkImage image;
    VkImageLayout imageLayout;
    VkDeviceMemory mem;
    VkImageView view;
    int32_t tex_width;
    int32_t tex_height;
  } texture_object;
  static const VkFormat kTexFmt = VK_FORMAT_R8G8B8A8_UNORM;

  struct VulkanBufferInfo {
    VkBuffer vertexBuf_;
  };
  VulkanBufferInfo buffers;

  struct VulkanGfxPipelineInfo {
    VkDescriptorSetLayout dscLayout_;
    VkDescriptorPool descPool_;
    VkDescriptorSet descSet_;
    VkPipelineLayout layout_;
    VkPipelineCache cache_;
    VkPipeline pipeline_;
  };
  VulkanGfxPipelineInfo gfxPipeline;

  struct VulkanRenderInfo {
    VkRenderPass renderPass_;
    VkCommandPool cmdPool_;
    VkCommandBuffer* cmdBuffer_;
    uint32_t cmdBufferLen_;
    VkSemaphore semaphore_;
    VkFence fence_;
  };
  VulkanRenderInfo renderInfo;

  ///////// Functions

  void CreateVulkanDevice(VkApplicationInfo* appInfo);

  void CreateSwapChain();

  ///////// Callbacks for AChoreographer and ALooper stored as private static functions

  static void doFrame(long timeStampNanos, void *data);
};
} // namespace android
} // namespace engine