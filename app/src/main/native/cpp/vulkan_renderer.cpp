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

void VulkanRenderer::createRenderPass() {
  VkAttachmentDescription attachmentDescriptions{
          .format = swapchain.displayFormat_,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  VkAttachmentReference colorReference = {
          .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpassDescription{
          .flags = 0,
          .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
          .inputAttachmentCount = 0,
          .pInputAttachments = nullptr,
          .colorAttachmentCount = 1,
          .pColorAttachments = &colorReference,
          .pResolveAttachments = nullptr,
          .pDepthStencilAttachment = nullptr,
          .preserveAttachmentCount = 0,
          .pPreserveAttachments = nullptr,
  };
  VkRenderPassCreateInfo renderPassCreateInfo{
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .pNext = nullptr,
          .attachmentCount = 1,
          .pAttachments = &attachmentDescriptions,
          .subpassCount = 1,
          .pSubpasses = &subpassDescription,
          .dependencyCount = 0,
          .pDependencies = nullptr,
  };
  CALL_VK(vkCreateRenderPass(device.device_, &renderPassCreateInfo, nullptr,
                             &renderInfo.renderPass_));
}

void VulkanRenderer::createVulkanDevice(VkApplicationInfo *appInfo) {
  std::vector<const char*> instance_extensions;
  std::vector<const char*> device_extensions;

  instance_extensions.push_back("VK_KHR_surface");
  instance_extensions.push_back("VK_KHR_android_surface");

  device_extensions.push_back("VK_KHR_swapchain");

  // **********************************************************
  // Create the Vulkan instance
  VkInstanceCreateInfo instanceCreateInfo{
          .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
          .pNext = nullptr,
          .pApplicationInfo = appInfo,
          .enabledLayerCount = 0,
          .ppEnabledLayerNames = nullptr,
          .enabledExtensionCount =
          static_cast<uint32_t>(instance_extensions.size()),
          .ppEnabledExtensionNames = instance_extensions.data(),
  };
  CALL_VK(vkCreateInstance(&instanceCreateInfo, nullptr, &device.instance_));
  VkAndroidSurfaceCreateInfoKHR createInfo{
          .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
          .pNext = nullptr,
          .flags = 0,
          .window = aNativeWindow};

  CALL_VK(vkCreateAndroidSurfaceKHR(device.instance_, &createInfo, nullptr,
                                    &device.surface_));
  // Find one GPU to use:
  // On Android, every GPU device is equal -- supporting
  // graphics/compute/present
  // for this sample, we use the very first GPU device found on the system
  uint32_t gpuCount = 0;
  CALL_VK(vkEnumeratePhysicalDevices(device.instance_, &gpuCount, nullptr));
  VkPhysicalDevice tmpGpus[gpuCount];
  CALL_VK(vkEnumeratePhysicalDevices(device.instance_, &gpuCount, tmpGpus));
  device.gpuDevice_ = tmpGpus[0];  // Pick up the first GPU Device

  vkGetPhysicalDeviceMemoryProperties(device.gpuDevice_,
                                      &device.gpuMemoryProperties_);

  // Find a GFX queue family
  uint32_t queueFamilyCount;
  vkGetPhysicalDeviceQueueFamilyProperties(device.gpuDevice_, &queueFamilyCount,
                                           nullptr);
  assert(queueFamilyCount);
  std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device.gpuDevice_, &queueFamilyCount,
                                           queueFamilyProperties.data());

  uint32_t queueFamilyIndex;
  for (queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount;
       queueFamilyIndex++) {
    if (queueFamilyProperties[queueFamilyIndex].queueFlags &
        VK_QUEUE_GRAPHICS_BIT) {
      break;
    }
  }
  assert(queueFamilyIndex < queueFamilyCount);
  device.queueFamilyIndex_ = queueFamilyIndex;
  // Create a logical device (vulkan device)
  float priorities[] = {
          1.0f,
  };
  VkDeviceQueueCreateInfo queueCreateInfo{
          .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .queueFamilyIndex = device.queueFamilyIndex_,
          .queueCount = 1,
          .pQueuePriorities = priorities,
  };

  VkDeviceCreateInfo deviceCreateInfo{
          .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
          .pNext = nullptr,
          .queueCreateInfoCount = 1,
          .pQueueCreateInfos = &queueCreateInfo,
          .enabledLayerCount = 0,
          .ppEnabledLayerNames = nullptr,
          .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
          .ppEnabledExtensionNames = device_extensions.data(),
          .pEnabledFeatures = nullptr,
  };

  CALL_VK(vkCreateDevice(device.gpuDevice_, &deviceCreateInfo, nullptr,
                         &device.device_));
  vkGetDeviceQueue(device.device_, 0, 0, &device.queue_);
}

void VulkanRenderer::createSwapChain() {
  LOGI("->createSwapChain");
  memset(&swapchain, 0, sizeof(swapchain));

  // **********************************************************
  // Get the surface capabilities because:
  //   - It contains the minimal and max length of the chain, we will need it
  //   - It's necessary to query the supported surface format (R8G8B8A8 for
  //   instance ...)
  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.gpuDevice_, device.surface_,
                                            &surfaceCapabilities);
  // Query the list of supported surface format and choose one we like
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device.gpuDevice_, device.surface_,
                                       &formatCount, nullptr);
  auto formats = new VkSurfaceFormatKHR[formatCount];
  vkGetPhysicalDeviceSurfaceFormatsKHR(device.gpuDevice_, device.surface_,
                                       &formatCount, formats);
  LOGI("Got %d formats", formatCount);

  uint32_t chosenFormat;
  for (chosenFormat = 0; chosenFormat < formatCount; chosenFormat++) {
    if (formats[chosenFormat].format == VK_FORMAT_R8G8B8A8_UNORM) break;
  }
  assert(chosenFormat < formatCount);

  swapchain.displaySize_ = surfaceCapabilities.currentExtent;
  LOGI("Display size w=%i, h=%i", swapchain.displaySize_.width, swapchain.displaySize_.height);
  swapchain.displayFormat_ = formats[chosenFormat].format;

  // **********************************************************
  // Create a swap chain (here we choose the minimum available number of surface
  // in the chain)
  VkSwapchainCreateInfoKHR swapchainCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
          .pNext = nullptr,
          .surface = device.surface_,
          // TODO according to https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain it's better to use `surfaceCapabilities.minImageCount + 1`, test it
          .minImageCount = surfaceCapabilities.minImageCount,
          .imageFormat = formats[chosenFormat].format,
          .imageColorSpace = formats[chosenFormat].colorSpace,
          .imageExtent = surfaceCapabilities.currentExtent,
          .imageArrayLayers = 1,
          .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
          .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 1,
          .pQueueFamilyIndices = &device.queueFamilyIndex_,
          .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
          .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
          .presentMode = VK_PRESENT_MODE_FIFO_KHR,
          // changed to true based on https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain
          .clipped = VK_TRUE,
          .oldSwapchain = VK_NULL_HANDLE,
  };
  CALL_VK(vkCreateSwapchainKHR(device.device_, &swapchainCreateInfo, nullptr,
                               &swapchain.swapchain_));

  // Get the length of the created swap chain
  CALL_VK(vkGetSwapchainImagesKHR(device.device_, swapchain.swapchain_,
                                  &swapchain.swapchainLength_, nullptr));
  delete[] formats;
  LOGI("<-createSwapChain");
}

void VulkanRenderer::createFrameBuffers() {
  // query display attachment to swapchain
  uint32_t swapchainImagesCount = 0;
  CALL_VK(vkGetSwapchainImagesKHR(device.device_, swapchain.swapchain_,
                                  &swapchainImagesCount, nullptr));
  swapchain.displayImages_ = new VkImage[swapchainImagesCount];
  CALL_VK(vkGetSwapchainImagesKHR(device.device_, swapchain.swapchain_,
                                  &swapchainImagesCount,
                                  swapchain.displayImages_));

  // create image view for each swapchain image
  swapchain.displayViews_ = new VkImageView[swapchainImagesCount];
  for (uint32_t i = 0; i < swapchainImagesCount; i++) {
    VkImageViewCreateInfo viewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = swapchain.displayImages_[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain.displayFormat_,
            .components =
                    {
                            .r = VK_COMPONENT_SWIZZLE_R,
                            .g = VK_COMPONENT_SWIZZLE_G,
                            .b = VK_COMPONENT_SWIZZLE_B,
                            .a = VK_COMPONENT_SWIZZLE_A,
                    },
            .subresourceRange =
                    {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                    },
    };
    CALL_VK(vkCreateImageView(device.device_, &viewCreateInfo, nullptr,
                              &swapchain.displayViews_[i]));
  }

  // create a framebuffer from each swapchain image
  swapchain.framebuffers_ = new VkFramebuffer[swapchain.swapchainLength_];
  for (uint32_t i = 0; i < swapchain.swapchainLength_; i++) {
    VkImageView attachments[2] = {
            swapchain.displayViews_[i], VK_NULL_HANDLE,
    };
    VkFramebufferCreateInfo fbCreateInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .renderPass = renderInfo.renderPass_,
            .attachmentCount = 1,  // 2 if using depth
            .pAttachments = attachments,
            .width = static_cast<uint32_t>(swapchain.displaySize_.width),
            .height = static_cast<uint32_t>(swapchain.displaySize_.height),
            .layers = 1,
    };
    fbCreateInfo.attachmentCount = 1;

    CALL_VK(vkCreateFramebuffer(device.device_, &fbCreateInfo, nullptr,
                                &swapchain.framebuffers_[i]));
  }
}

void VulkanRenderer::createGraphicsPipeline() {
  LOGI("->createGraphicsPipeline");
  memset(&gfxPipeline, 0, sizeof(gfxPipeline));

  const VkDescriptorSetLayoutBinding descriptorSetLayoutBinding{
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr,
  };
  const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .pNext = nullptr,
          .bindingCount = 1,
          .pBindings = &descriptorSetLayoutBinding,
  };
  CALL_VK(vkCreateDescriptorSetLayout(device.device_,
                                      &descriptorSetLayoutCreateInfo, nullptr,
                                      &gfxPipeline.dscLayout_));
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .pNext = nullptr,
          .setLayoutCount = 1,
          .pSetLayouts = &gfxPipeline.dscLayout_,
          .pushConstantRangeCount = 0,
          .pPushConstantRanges = nullptr,
  };
  CALL_VK(vkCreatePipelineLayout(device.device_, &pipelineLayoutCreateInfo,
                                 nullptr, &gfxPipeline.layout_));

  // No dynamic state in that tutorial
  VkPipelineDynamicStateCreateInfo dynamicStateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
          .pNext = nullptr,
          .dynamicStateCount = 0,
          .pDynamicStates = nullptr};

  VkShaderModule vertexShader, fragmentShader;
  buildShaderFromFile(vertexShaderSource,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      device.device_,
                      &vertexShader);
  buildShaderFromFile(fragmentShaderSource,
                      VK_SHADER_STAGE_FRAGMENT_BIT,
                      device.device_,
                      &fragmentShader);
  // Specify vertex and fragment shader stages
  VkPipelineShaderStageCreateInfo shaderStages[2]{
          {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .pNext = nullptr,
                  .flags = 0,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vertexShader,
                  .pName = "main",
                  .pSpecializationInfo = nullptr,
          },
          {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .pNext = nullptr,
                  .flags = 0,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = fragmentShader,
                  .pName = "main",
                  .pSpecializationInfo = nullptr,
          }};

  VkViewport viewports {
          .x = 0,
          .y = 0,
          .width = (float)swapchain.displaySize_.width,
          .height = (float)swapchain.displaySize_.height,
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
  };

  VkRect2D scissor = {
          .offset = {.x = 0, .y = 0,},
          .extent = swapchain.displaySize_,
  };
  // Specify viewport info
  VkPipelineViewportStateCreateInfo viewportInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
          .pNext = nullptr,
          .viewportCount = 1,
          .pViewports = &viewports,
          .scissorCount = 1,
          .pScissors = &scissor,
  };

  // Specify multisample info
  VkSampleMask sampleMask = ~0u;
  VkPipelineMultisampleStateCreateInfo multisampleInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
          .pNext = nullptr,
          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
          .sampleShadingEnable = VK_FALSE,
          .minSampleShading = 0,
          .pSampleMask = &sampleMask,
          .alphaToCoverageEnable = VK_FALSE,
          .alphaToOneEnable = VK_FALSE,
  };

  // Specify color blend state
  VkPipelineColorBlendAttachmentState attachmentStates{
          .blendEnable = VK_FALSE,
          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };
  VkPipelineColorBlendStateCreateInfo colorBlendInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .logicOpEnable = VK_FALSE,
          .logicOp = VK_LOGIC_OP_COPY,
          .attachmentCount = 1,
          .pAttachments = &attachmentStates,
  };

  // Specify rasterizer info
  VkPipelineRasterizationStateCreateInfo rasterInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
          .pNext = nullptr,
          .depthClampEnable = VK_FALSE,
          .rasterizerDiscardEnable = VK_FALSE,
          .polygonMode = VK_POLYGON_MODE_FILL,
          .cullMode = VK_CULL_MODE_NONE,
          .frontFace = VK_FRONT_FACE_CLOCKWISE,
          .depthBiasEnable = VK_FALSE,
          .lineWidth = 1,
  };

  // Specify input assembler state
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
          .pNext = nullptr,
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          .primitiveRestartEnable = VK_FALSE,
  };

  // Specify vertex input state
  VkVertexInputBindingDescription vertex_input_bindings{
          .binding = 0,
          .stride = 5 * sizeof(float),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };
  VkVertexInputAttributeDescription vertex_input_attributes[2]{
          {
                  .location = 0,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32B32_SFLOAT,
                  .offset = 0,
          },
          {
                  .location = 1,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset = sizeof(float) * 3,
          }};
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          .pNext = nullptr,
          .vertexBindingDescriptionCount = 1,
          .pVertexBindingDescriptions = &vertex_input_bindings,
          .vertexAttributeDescriptionCount = 2,
          .pVertexAttributeDescriptions = vertex_input_attributes,
  };

  // Create the pipeline cache
  VkPipelineCacheCreateInfo pipelineCacheInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,  // reserved, must be 0
          .initialDataSize = 0,
          .pInitialData = nullptr,
  };

  CALL_VK(vkCreatePipelineCache(device.device_, &pipelineCacheInfo, nullptr,
                                &gfxPipeline.cache_));

  // Create the pipeline
  VkGraphicsPipelineCreateInfo pipelineCreateInfo{
          .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .stageCount = 2,
          .pStages = shaderStages,
          .pVertexInputState = &vertexInputInfo,
          .pInputAssemblyState = &inputAssemblyInfo,
          .pTessellationState = nullptr,
          .pViewportState = &viewportInfo,
          .pRasterizationState = &rasterInfo,
          .pMultisampleState = &multisampleInfo,
          .pDepthStencilState = nullptr,
          .pColorBlendState = &colorBlendInfo,
          .pDynamicState = &dynamicStateInfo,
          .layout = gfxPipeline.layout_,
          .renderPass = renderInfo.renderPass_,
          .subpass = 0,
          .basePipelineHandle = VK_NULL_HANDLE,
          .basePipelineIndex = 0,
  };

  VkResult pipelineResult = vkCreateGraphicsPipelines(
          device.device_, gfxPipeline.cache_, 1, &pipelineCreateInfo, nullptr,
          &gfxPipeline.pipeline_);

  // We don't need the shaders anymore, we can release their memory
  vkDestroyShaderModule(device.device_, vertexShader, nullptr);
  vkDestroyShaderModule(device.device_, fragmentShader, nullptr);
  LOGI("<-createGraphicsPipeline");
}

shaderc_shader_kind VulkanRenderer::getShadercShaderType(VkShaderStageFlagBits type) {
  switch (type) {
    case VK_SHADER_STAGE_VERTEX_BIT:
      return shaderc_glsl_vertex_shader;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
      return shaderc_glsl_fragment_shader;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      return shaderc_glsl_tess_control_shader;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      return shaderc_glsl_tess_evaluation_shader;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
      return shaderc_glsl_geometry_shader;
    case VK_SHADER_STAGE_COMPUTE_BIT:
      return shaderc_glsl_compute_shader;
    default:
      LOGE("invalid VKShaderStageFlagBits: type = %08x", type);
  }
  return static_cast<shaderc_shader_kind>(-1);
}

VkResult VulkanRenderer::buildShaderFromFile(const char* shaderSource,
                                             VkShaderStageFlagBits type, VkDevice vkDevice,
                                             VkShaderModule* shaderOut) {
  // compile into spir-V shader
  shaderc_compiler_t compiler = shaderc_compiler_initialize();
  shaderc_compilation_result_t spvShader = shaderc_compile_into_spv(
          compiler, shaderSource, strlen(shaderSource), getShadercShaderType(type),
          "shaderc_error", "main", nullptr);
  if (shaderc_result_get_compilation_status(spvShader) !=
      shaderc_compilation_status_success) {
    return static_cast<VkResult>(-1);
  }

  // build vulkan shader module
  VkShaderModuleCreateInfo shaderModuleCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .codeSize = shaderc_result_get_length(spvShader),
          .pCode = (const uint32_t*)shaderc_result_get_bytes(spvShader),
  };
  VkResult result = vkCreateShaderModule(vkDevice, &shaderModuleCreateInfo,
                                         nullptr, shaderOut);

  shaderc_result_release(spvShader);
  shaderc_compiler_release(compiler);

  return result;
}

} // namespace android
} // namespace engine
