#pragma once

#include <cassert>
#include <shaderc/shaderc.hpp>

#include "base_renderer.hpp"
#include "frame_trace.hpp"
#include "vulkan_wrapper.h"

namespace engine {
namespace android {

class VulkanRenderer : public BaseRenderer {
protected:

  const char *renderingModeName() override {
    return (const char *) "Vulkan";
  }

  const char *frameToNativeSectionName() const override { return traceNames::FRAME_TO_NATIVE_VK; }
  const char *frameNativeProcSectionName() const override { return traceNames::FRAME_NATIVE_PROC_VK; }
  const char *frameToScreenSectionName() const override { return traceNames::FRAME_TO_SCREEN_VK; }
  const char *frameE2ESectionName() const override { return traceNames::FRAME_E2E_VK; }
  const char *droppedFramesCounterName() const override { return traceNames::DROPPED_FRAMES_VK; }

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
    deviceInfo.initialized = true;
    LOGI("<-onWindowCreated");
    return true;
  }

  // Per-tick hook is a no-op for Vulkan; the swapchain re-fit happens in onRefit instead.
  void onWindowSizeUpdated(int width, int height) override {}

  void onRefit(int width, int height) override {
    if (!deviceInfo.initialized) {
      return;
    }
    if (width != swapchainInfo.displaySize.width || height != swapchainInfo.displaySize.height) {
      recreateSwapChain(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
  }

  void onWindowDestroyed() override {
    if (!deviceInfo.initialized) {
      return;
    }
    CALL_VK(vkDeviceWaitIdle(deviceInfo.device))
    cleanup();
    deviceInfo.initialized = false;
    cameraInitialized = false;
  }

  void hwBufferToTexture(AHardwareBuffer *buffer) override;

  void onMvpUpdated() override;

  bool couldRender() const override {
    return deviceInfo.initialized && cameraInitialized;
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

  bool cameraInitialized = false;

  struct UniformBufferObject {
    glm::mat4 mvp;
  };

  struct VulkanDeviceInfo {
    bool initialized;

    VkInstance instance;
    VkPhysicalDevice gpuDevice;
    VkPhysicalDeviceMemoryProperties gpuMemoryProperties;
    VkDevice device;
    uint32_t queueFamilyIndex;

    VkSurfaceKHR surface;
    VkQueue queue;
  };
  VulkanDeviceInfo deviceInfo{};

  struct VulkanSwapchainInfo {
    VkSwapchainKHR swapchain;
    uint32_t swapchainLength;

    VkExtent2D displaySize;
    VkFormat displayFormat;

    // array of frame buffers and views
    VkFramebuffer* framebuffers;
    VkImage* displayImages;
    VkImageView* displayViews;
  };
  VulkanSwapchainInfo swapchainInfo{};

  struct VulkanExternalTextureInfo {
    VkSampler sampler;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
  };
  VulkanExternalTextureInfo externalTextureInfo{};

  struct VulkanBuffersInfo {
    VkBuffer vertexBuf;
    VkBuffer uniformBuf;
    VkDeviceMemory uniformBufferMemory;
    VkDeviceMemory vertexBufferMemory;
    void* uniformBufferMapped;
  };
  VulkanBuffersInfo buffersInfo{};

  struct VulkanGfxPipelineInfo {
    VkDescriptorSetLayout dscLayout;
    VkDescriptorPool descPool;
    VkDescriptorSet descSet;
    VkPipelineLayout layout;
    VkPipelineCache cache;
    VkPipeline pipeline;
    VkWriteDescriptorSet* descWrites;
  };
  VulkanGfxPipelineInfo gfxPipelineInfo{};

  struct VulkanRenderInfo {
    VkRenderPass renderPass;
    VkCommandPool cmdPool;
    VkCommandBuffer* cmdBuffer;
    uint32_t cmdBufferLen;
    VkSemaphore semaphore;
    VkFence fence;
  };
  VulkanRenderInfo renderInfo{};

  ///////// Create functions

  void createVulkanDevice(VkApplicationInfo* appInfo);

  void createSwapChain(uint32_t width = 0, uint32_t height = 0,
                       VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

  void createRenderPass();

  void createFrameBuffersAndImages();

  void createVertexBuffer();

  void createGraphicsPipeline();

  void createDescriptorSet();

  void createUniformBuffer();

  void createOtherStaff();

  void recordCommandBuffer();

  // Tear down + recreate the swapchain at (width, height) and re-record command buffers.
  // Pass (0, 0) to use the current surface extent.
  void recreateSwapChain(uint32_t width, uint32_t height);

  ////// Destroy functions

  void cleanupSwapChain();

  void cleanup();

  ////// Helper functions

  void mapMemoryTypeToIndex(uint32_t typeBits, VkFlags requirements_mask, uint32_t* typeIndex) const;

  void createBuffer(VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer& buffer,
                    VkDeviceMemory& bufferMemory);

  static void setImageLayout(VkCommandBuffer cmdBuffer, VkImage image,
                      VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
                      VkPipelineStageFlags srcStages,
                      VkPipelineStageFlags destStages);

  static VkResult buildShaderFromFile(const char* shaderSource,
                               VkShaderStageFlagBits type,
                               VkDevice vkDevice,
                               VkShaderModule* shaderOut);

  static shaderc_shader_kind getShadercShaderType(VkShaderStageFlagBits type);

  ///////// Callbacks for AChoreographer and ALooper stored as private static functions

  void renderImpl();

  static void doFrame(long timeStampNanos, void *data);
};
} // namespace android
} // namespace engine