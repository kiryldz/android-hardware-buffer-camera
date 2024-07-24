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
    LOGI("->onWindowCreated");
    VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "fast_camera",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "vulkan_engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_3,
    };

    createVulkanDevice(&appInfo);
    createSwapChain();
    createRenderPass();
    createFrameBuffersAndImages();
    createVertexBuffer();
    createUniformBuffer();
    createGraphicsPipeline();
    createDescriptorSet();
    createOtherStaff();
    device.initialized_ = true;
    LOGI("<-onWindowCreated");
    return true;
  }

  void onWindowSizeUpdated(int width, int height) override {
    if (width != swapchain.displaySize_.width || height != swapchain.displaySize_.height) {
      LOGI("->onWindowSizeUpdated");
      CALL_VK(vkDeviceWaitIdle(device.device_))
      cleanupSwapChain();
      createSwapChain(width, height);
      createFrameBuffersAndImages();
      LOGI("<-onWindowSizeUpdated");
    }
  }

  void onWindowDestroyed() override {
    CALL_VK(vkDeviceWaitIdle(device.device_))
    cleanup();
    device.initialized_ = false;
    device.cameraInitialized_ = false;
  }

  void hwBufferToTexture(AHardwareBuffer *buffer) override;

  void onMvpUpdated() override;

  bool couldRender() const override {
    return device.initialized_ && device.cameraInitialized_;
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
  const char *vertexShaderSource = "#version 450\n"
                                   "#extension GL_ARB_separate_shader_objects : enable\n"
                                   "#extension GL_ARB_shading_language_420pack : enable\n"
                                   "layout (binding = 0) uniform UniformBufferObject {\n"
                                   "    mat4 mvp;\n"
                                   "} ubo;\n"
                                   "layout (location = 0) in vec2 pos;\n"
                                   "layout (location = 1) in vec2 attr;\n"
                                   "layout (location = 0) out vec2 texcoord;\n"
                                   "void main() {\n"
                                   "   texcoord = attr;\n"
                                   "   gl_Position = ubo.mvp * vec4(pos, 0.0, 1.0);\n"
                                   "}";

  const char  *fragmentShaderSource = "#version 450\n"
                                      "#extension GL_ARB_separate_shader_objects : enable\n"
                                      "#extension GL_ARB_shading_language_420pack : enable\n"
                                      "layout (binding = 1) uniform sampler2D tex;\n"
                                      "layout (location = 0) in vec2 texcoord;\n"
                                      "layout (location = 0) out vec4 uFragColor;\n"
                                      "void main() {\n"
                                      "   uFragColor = texture(tex, texcoord);\n"
                                      "}";

  ///////// Structs and variables

  struct UniformBufferObject {
    glm::mat4 mvp;
  };

  struct VulkanDeviceInfo {
    bool initialized_;
    bool cameraInitialized_;

    VkInstance instance_;
    VkPhysicalDevice gpuDevice_;
    VkPhysicalDeviceMemoryProperties gpuMemoryProperties_;
    VkDevice device_;
    uint32_t queueFamilyIndex_;

    VkSurfaceKHR surface_;
    VkQueue queue_;
  };
  VulkanDeviceInfo device;

  typedef struct VulkanSwapchainInfo {
    VkSwapchainKHR swapchain_;
    uint32_t swapchainLength_;

    VkExtent2D displaySize_;
    VkFormat displayFormat_;

    // array of frame buffers and views
    VkFramebuffer* framebuffers_;
    VkImage* displayImages_;
    VkImageView* displayViews_;
  } VulkanSwapchainInfo;
  VulkanSwapchainInfo swapchain;

  typedef struct VulkanExternalTextureInfo {
    VkSampler sampler;
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
  } VulkanExternalTextureInfo;
  static const VkFormat kTexFmt = VK_FORMAT_R8G8B8A8_UNORM;
  struct VulkanExternalTextureInfo tex;

  struct VulkanBufferInfo {
    VkBuffer vertexBuf;
    VkBuffer uniformBuf;
    VkDeviceMemory uniformBufferMemory;
    VkDeviceMemory vertexBufferMemory;
    void* uniformBufferMapped;
  };
  VulkanBufferInfo buffers;

  typedef struct VulkanGfxPipelineInfo {
    VkDescriptorSetLayout dscLayout_;
    VkDescriptorPool descPool_;
    VkDescriptorSet descSet_;
    VkPipelineLayout layout_;
    VkPipelineCache cache_;
    VkPipeline pipeline_;
    VkWriteDescriptorSet* descWrites_;
  } VulkanGfxPipelineInfo;
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

  void createSwapChain(uint32_t width = 0, uint32_t height = 0);

  void createRenderPass();

  void createFrameBuffersAndImages();

  void createVertexBuffer();

  void createGraphicsPipeline();

  void createDescriptorSet();

  void createUniformBuffer();

  void createOtherStaff();

  void recordCommandBuffer();

  ////// Destroy functions

  void cleanupSwapChain();

  void cleanup();

  ////// Helper functions

  void mapMemoryTypeToIndex(uint32_t typeBits, VkFlags requirements_mask, uint32_t* typeIndex);

  void createBuffer(VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer& buffer,
                    VkDeviceMemory& bufferMemory);

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