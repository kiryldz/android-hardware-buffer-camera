#pragma once

#include <cassert>
#include <shaderc/shaderc.hpp>

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
            .pApplicationName = "fast_camera",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "fast_camera",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_MAKE_VERSION(1, 0, 0),
    };

    createVulkanDevice(&appInfo);
    createSwapChain();
    createRenderPass();
    createFrameBuffers();
    createTexture();
    createBuffers();
    createGraphicsPipeline();
    putAllTogether();
    device.initialized_ = true;
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
    return device.initialized_;
  }

  void render() override {
    renderImpl();
  }

  void postChoreographerCallback() override {
    // posting next frame callback, no need to explicitly wake the looper afterwards
    // as AChoreographer seems to operate with it's own fd and callbacks
    AChoreographer_postFrameCallback(aChoreographer, doFrame, this);
  }

private:
  ///////// Shaders
  const char *vertexShaderSource = "#version 400\n"
                                   "#extension GL_ARB_separate_shader_objects : enable\n"
                                   "#extension GL_ARB_shading_language_420pack : enable\n"
                                   "layout (location = 0) in vec4 pos;\n"
                                   "layout (location = 1) in vec2 attr;\n"
                                   "layout (location = 0) out vec2 texcoord;\n"
                                   "void main() {\n"
                                   "   texcoord = attr;\n"
                                   "   gl_Position = pos;\n"
                                   "}";

  const char  *fragmentShaderSource = "#version 400\n"
                                      "#extension GL_ARB_separate_shader_objects : enable\n"
                                      "#extension GL_ARB_shading_language_420pack : enable\n"
                                      "layout (binding = 0) uniform sampler2D tex;\n"
                                      "layout (location = 0) in vec2 texcoord;\n"
                                      "layout (location = 0) out vec4 uFragColor;\n"
                                      "void main() {\n"
                                      "   uFragColor = texture(tex, texcoord);\n"
                                      "}";

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

  ///////// Create functions

  void createVulkanDevice(VkApplicationInfo* appInfo);

  void createSwapChain();

  void createRenderPass();

  void createFrameBuffers();

  void createTexture();

  void createBuffers();

  void createGraphicsPipeline();

  void putAllTogether();

  ////// Helper functions

  void mapMemoryTypeToIndex(uint32_t typeBits, VkFlags requirements_mask, uint32_t* typeIndex);

  void setImageLayout(VkCommandBuffer cmdBuffer, VkImage image,
                      VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
                      VkPipelineStageFlags srcStages,
                      VkPipelineStageFlags destStages);

  VkResult buildShaderFromFile(const char* shaderSource,
                               VkShaderStageFlagBits type,
                               VkDevice vkDevice,
                               VkShaderModule* shaderOut);

  shaderc_shader_kind getShadercShaderType(VkShaderStageFlagBits type);

  ///////// Callbacks for AChoreographer and ALooper stored as private static functions

  void renderImpl();

  static void doFrame(long timeStampNanos, void *data);
};
} // namespace android
} // namespace engine