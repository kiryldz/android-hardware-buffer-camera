#include "vulkan_renderer.hpp"

namespace engine {
namespace android {


void VulkanRenderer::doFrame(long timeStampNanos, void *data) {
  auto *renderer = reinterpret_cast<engine::android::VulkanRenderer *>(data);
  if (renderer->couldRender()) {
    renderer->render();
  }
}

void VulkanRenderer::createRenderPass() {
  VkAttachmentDescription attachmentDescriptions{
          .format = swapchainInfo.displayFormat,
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
  CALL_VK(vkCreateRenderPass(deviceInfo.device, &renderPassCreateInfo, nullptr,
                             &renderInfo.renderPass))
}

void VulkanRenderer::createVulkanDevice(VkApplicationInfo *appInfo) {
  std::vector<const char *> instance_extensions;
  std::vector<const char *> device_extensions;
  std::vector<const char *> validation_layers;

  instance_extensions.push_back("VK_KHR_surface");
  instance_extensions.push_back("VK_KHR_android_surface");

  device_extensions.push_back("VK_KHR_swapchain");
  device_extensions.push_back("VK_ANDROID_external_memory_android_hardware_buffer");
  device_extensions.push_back("VK_EXT_queue_family_foreign");

#ifndef NDEBUG
  validation_layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  // **********************************************************
  // Create the Vulkan instance
  VkInstanceCreateInfo instanceCreateInfo{
          .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
          .pNext = nullptr,
          .pApplicationInfo = appInfo,
          .enabledLayerCount = static_cast<uint32_t>(validation_layers.size()),
          .ppEnabledLayerNames = validation_layers.data(),
          .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
          .ppEnabledExtensionNames = instance_extensions.data(),
  };
  CALL_VK(vkCreateInstance(&instanceCreateInfo, nullptr, &deviceInfo.instance))
  VkAndroidSurfaceCreateInfoKHR createInfo{
          .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
          .pNext = nullptr,
          .flags = 0,
          .window = aNativeWindow
  };

  CALL_VK(vkCreateAndroidSurfaceKHR(deviceInfo.instance, &createInfo, nullptr, &deviceInfo.surface))
  // Find one GPU to use:
  // On Android, every GPU device is equal -- supporting
  // graphics/compute/present
  // for this sample, we use the very first GPU device found on the system
  uint32_t gpuCount = 0;
  CALL_VK(vkEnumeratePhysicalDevices(deviceInfo.instance, &gpuCount, nullptr))
  VkPhysicalDevice tmpGpus[gpuCount];
  CALL_VK(vkEnumeratePhysicalDevices(deviceInfo.instance, &gpuCount, tmpGpus))
  deviceInfo.gpuDevice = tmpGpus[0];  // Pick up the first GPU Device

  vkGetPhysicalDeviceMemoryProperties(deviceInfo.gpuDevice, &deviceInfo.gpuMemoryProperties);

  // Find a GFX queue family
  uint32_t queueFamilyCount;
  vkGetPhysicalDeviceQueueFamilyProperties(deviceInfo.gpuDevice, &queueFamilyCount, nullptr);
  assert(queueFamilyCount);
  std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(deviceInfo.gpuDevice, &queueFamilyCount,
                                           queueFamilyProperties.data());

  uint32_t queueFamilyIndex;
  for (queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++) {
    if (queueFamilyProperties[queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      break;
    }
  }
  assert(queueFamilyIndex < queueFamilyCount);
  deviceInfo.queueFamilyIndex = queueFamilyIndex;
  // Create a logical device (vulkan device)
  float priorities[] = {
          1.0f,
  };
  VkDeviceQueueCreateInfo queueCreateInfo{
          .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .queueFamilyIndex = deviceInfo.queueFamilyIndex,
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

  CALL_VK(vkCreateDevice(deviceInfo.gpuDevice, &deviceCreateInfo, nullptr,
                         &deviceInfo.device))
  vkGetDeviceQueue(deviceInfo.device, 0, 0, &deviceInfo.queue);
}

void VulkanRenderer::createSwapChain(uint32_t width, uint32_t height) {
  LOGI("->createSwapChain");
  // **********************************************************
  // Get the surface capabilities because:
  //   - It contains the minimal and max length of the chain, we will need it
  //   - It's necessary to query the supported surface format (R8G8B8A8 for instance ...)
  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(deviceInfo.gpuDevice, deviceInfo.surface,
                                            &surfaceCapabilities);
  // Query the list of supported surface format and choose one we like
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(deviceInfo.gpuDevice, deviceInfo.surface,
                                       &formatCount, nullptr);
  auto formats = new VkSurfaceFormatKHR[formatCount];
  vkGetPhysicalDeviceSurfaceFormatsKHR(deviceInfo.gpuDevice, deviceInfo.surface,
                                       &formatCount, formats);
  LOGI("Got %d formats", formatCount);

  uint32_t chosenFormat;
  for (chosenFormat = 0; chosenFormat < formatCount; chosenFormat++) {
    if (formats[chosenFormat].format == VK_FORMAT_R8G8B8A8_UNORM) break;
  }
  assert(chosenFormat < formatCount);

  if (width == 0 && height == 0) {
    swapchainInfo.displaySize = surfaceCapabilities.currentExtent;
  } else {
    swapchainInfo.displaySize = VkExtent2D{
            .width = width,
            .height = height
    };
  }
  LOGI("Display size w=%i, h=%i", swapchainInfo.displaySize.width,
       swapchainInfo.displaySize.height);
  swapchainInfo.displayFormat = formats[chosenFormat].format;

  // **********************************************************
  // Create a swap chain (here we choose the minimum available number of surface
  // in the chain)
  VkSwapchainCreateInfoKHR swapchainCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
          .pNext = nullptr,
          .surface = deviceInfo.surface,
          .minImageCount = surfaceCapabilities.minImageCount,
          .imageFormat = formats[chosenFormat].format,
          .imageColorSpace = formats[chosenFormat].colorSpace,
          .imageExtent = swapchainInfo.displaySize,
          .imageArrayLayers = 1,
          .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
          .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 1,
          .pQueueFamilyIndices = &deviceInfo.queueFamilyIndex,
          .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
          .compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          .presentMode = VK_PRESENT_MODE_FIFO_KHR,
          // changed to true based on https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain
          .clipped = VK_TRUE,
          .oldSwapchain = VK_NULL_HANDLE,
  };
  CALL_VK(vkCreateSwapchainKHR(deviceInfo.device, &swapchainCreateInfo, nullptr,
                               &swapchainInfo.swapchain))
  delete[] formats;
  LOGI("<-createSwapChain");
}

void VulkanRenderer::createFrameBuffersAndImages() {
  LOGI("->createFrameBuffers");
  // Get the length of the created swap chain
  CALL_VK(vkGetSwapchainImagesKHR(deviceInfo.device, swapchainInfo.swapchain,
                                  &swapchainInfo.swapchainLength, nullptr))
  assert(swapchainInfo.swapchainLength > 1);
  swapchainInfo.displayImages = new VkImage[swapchainInfo.swapchainLength];
  CALL_VK(vkGetSwapchainImagesKHR(deviceInfo.device, swapchainInfo.swapchain,
                                  &swapchainInfo.swapchainLength,
                                  swapchainInfo.displayImages))
  // create image view for each swapchain image
  swapchainInfo.displayViews = new VkImageView[swapchainInfo.swapchainLength];
  for (uint32_t i = 0; i < swapchainInfo.swapchainLength; i++) {
    VkImageViewCreateInfo viewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = swapchainInfo.displayImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainInfo.displayFormat,
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
    CALL_VK(vkCreateImageView(deviceInfo.device, &viewCreateInfo, nullptr,
                              &swapchainInfo.displayViews[i]))
  }

  // create a framebuffer from each swapchain image
  swapchainInfo.framebuffers = new VkFramebuffer[swapchainInfo.swapchainLength];
  for (uint32_t i = 0; i < swapchainInfo.swapchainLength; i++) {
    VkImageView attachments[2] = {
            swapchainInfo.displayViews[i], VK_NULL_HANDLE,
    };
    VkFramebufferCreateInfo fbCreateInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .renderPass = renderInfo.renderPass,
            .attachmentCount = 1,  // 2 if using depth
            .pAttachments = attachments,
            .width = static_cast<uint32_t>(swapchainInfo.displaySize.width),
            .height = static_cast<uint32_t>(swapchainInfo.displaySize.height),
            .layers = 1,
    };

    LOGI("Creating framebuffer №%d w=%d, h=%d", i, swapchainInfo.displaySize.width,
         swapchainInfo.displaySize.height);
    CALL_VK(vkCreateFramebuffer(deviceInfo.device, &fbCreateInfo, nullptr,
                                &swapchainInfo.framebuffers[i]))
  }
  LOGI("<-createFrameBuffers");
}

void VulkanRenderer::createUniformBuffer() {
  createBuffer(
          sizeof(UniformBufferObject),
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          buffersInfo.uniformBuf,
          buffersInfo.uniformBufferMemory
  );
  CALL_VK(vkMapMemory(
          deviceInfo.device,
          buffersInfo.uniformBufferMemory,
          0,
          sizeof(UniformBufferObject),
          0,
          &buffersInfo.uniformBufferMapped)
  )
}

void VulkanRenderer::createGraphicsPipeline() {
  LOGI("->createGraphicsPipeline");
  const VkDescriptorSetLayoutBinding uboLayoutBinding = {
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .pImmutableSamplers = nullptr,
  };
  const VkDescriptorSetLayoutBinding imageLayoutBinding{
          .binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr,
  };
  const auto bindings = new VkDescriptorSetLayoutBinding[2];
  bindings[0] = uboLayoutBinding;
  bindings[1] = imageLayoutBinding;
  const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = 2,
          .pBindings = bindings,
  };
  CALL_VK(vkCreateDescriptorSetLayout(deviceInfo.device,
                                      &descriptorSetLayoutCreateInfo, nullptr,
                                      &gfxPipelineInfo.dscLayout))
  delete[] bindings;
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .pNext = nullptr,
          .setLayoutCount = 1,
          .pSetLayouts = &gfxPipelineInfo.dscLayout,
          .pushConstantRangeCount = 0,
          .pPushConstantRanges = nullptr,
  };
  CALL_VK(vkCreatePipelineLayout(deviceInfo.device, &pipelineLayoutCreateInfo, nullptr,
                                 &gfxPipelineInfo.layout))
  auto dynamicStates = new VkDynamicState[2]{
          VK_DYNAMIC_STATE_VIEWPORT,
          VK_DYNAMIC_STATE_SCISSOR
  };
  VkPipelineDynamicStateCreateInfo dynamicStateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
          .pNext = nullptr,
          .dynamicStateCount = 2,
          .pDynamicStates = dynamicStates
  };

  VkShaderModule vertexShader, fragmentShader;
  buildShaderFromFile(vertexShaderSource,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      deviceInfo.device,
                      &vertexShader);
  buildShaderFromFile(fragmentShaderSource,
                      VK_SHADER_STAGE_FRAGMENT_BIT,
                      deviceInfo.device,
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

  VkViewport viewports{
          .x = 0,
          .y = 0,
          .width = (float) swapchainInfo.displaySize.width,
          .height = (float) swapchainInfo.displaySize.height,
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
  };

  VkRect2D scissor = {
          .offset = {.x = 0, .y = 0,},
          .extent = swapchainInfo.displaySize,
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
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
          .primitiveRestartEnable = VK_FALSE,
  };

  // Specify vertex input state
  VkVertexInputBindingDescription vertex_input_bindings{
          .binding = 0,
          .stride = 4 * sizeof(float),
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
                  .offset = sizeof(float) * 2,
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

  CALL_VK(vkCreatePipelineCache(deviceInfo.device, &pipelineCacheInfo, nullptr,
                                &gfxPipelineInfo.cache))

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
          .layout = gfxPipelineInfo.layout,
          .renderPass = renderInfo.renderPass,
          .subpass = 0,
          .basePipelineHandle = VK_NULL_HANDLE,
          .basePipelineIndex = 0,
  };

  CALL_VK(vkCreateGraphicsPipelines(
          deviceInfo.device, gfxPipelineInfo.cache, 1, &pipelineCreateInfo, nullptr,
          &gfxPipelineInfo.pipeline))

  // We don't need the shaders anymore, we can release their memory
  vkDestroyShaderModule(deviceInfo.device, vertexShader, nullptr);
  vkDestroyShaderModule(deviceInfo.device, fragmentShader, nullptr);
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

VkResult VulkanRenderer::buildShaderFromFile(const char *shaderSource,
                                             VkShaderStageFlagBits type, VkDevice vkDevice,
                                             VkShaderModule *shaderOut) {
  // compile into Spir-V shader
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
          .pCode = (const uint32_t *) shaderc_result_get_bytes(spvShader),
  };
  VkResult result = vkCreateShaderModule(vkDevice, &shaderModuleCreateInfo,
                                         nullptr, shaderOut);

  shaderc_result_release(spvShader);
  shaderc_compiler_release(compiler);

  return result;
}

void VulkanRenderer::createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer &buffer,
        VkDeviceMemory &bufferMemory
) {
  VkBufferCreateInfo createBufferInfo{
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .size = size,
          .usage = usage,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 1,
          .pQueueFamilyIndices = &deviceInfo.queueFamilyIndex,
  };
  CALL_VK(vkCreateBuffer(deviceInfo.device, &createBufferInfo, nullptr,
                         &buffer))
  VkMemoryRequirements memReq;
  vkGetBufferMemoryRequirements(deviceInfo.device, buffer, &memReq);
  VkMemoryAllocateInfo allocInfo{
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = nullptr,
          .allocationSize = memReq.size,
          .memoryTypeIndex = 0,  // Memory type assigned in the next step
  };
  // Assign the proper memory type for that buffer
  mapMemoryTypeToIndex(memReq.memoryTypeBits,
                       properties,
                       &allocInfo.memoryTypeIndex);
  CALL_VK(vkAllocateMemory(deviceInfo.device, &allocInfo, nullptr, &bufferMemory))
  CALL_VK(vkBindBufferMemory(deviceInfo.device, buffer, bufferMemory, 0))
}

void VulkanRenderer::mapMemoryTypeToIndex(uint32_t typeBits,
                                          VkFlags requirements_mask,
                                          uint32_t *typeIndex) const {
  VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(deviceInfo.gpuDevice, &memoryProperties);
  // Search memtypes to find first index with those properties
  for (uint32_t i = 0; i < 32; i++) {
    if ((typeBits & 1) == 1) {
      // Type is available, does it match user properties?
      if ((memoryProperties.memoryTypes[i].propertyFlags & requirements_mask) ==
          requirements_mask) {
        *typeIndex = i;
        return;
      }
    }
    typeBits >>= 1;
  }
}

void VulkanRenderer::setImageLayout(VkCommandBuffer cmdBuffer, VkImage image,
                                    VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
                                    VkPipelineStageFlags srcStages,
                                    VkPipelineStageFlags destStages) {
  VkImageMemoryBarrier imageMemoryBarrier = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = 0,
          .dstAccessMask = 0,
          .oldLayout = oldImageLayout,
          .newLayout = newImageLayout,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = image,
          .subresourceRange =
                  {
                          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                          .baseMipLevel = 0,
                          .levelCount = 1,
                          .baseArrayLayer = 0,
                          .layerCount = 1,
                  },
  };

  switch (oldImageLayout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_PREINITIALIZED:
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
      break;

    default:
      break;
  }

  switch (newImageLayout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      imageMemoryBarrier.dstAccessMask =
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;

    default:
      break;
  }

  vkCmdPipelineBarrier(cmdBuffer, srcStages, destStages, 0, 0, nullptr, 0, nullptr, 1,
                       &imageMemoryBarrier);
}

void VulkanRenderer::createOtherStaff() {
  LOGI("->createOtherStaff");
  // Create sampler
  const VkSamplerCreateInfo sampler = {
          .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
          .pNext = nullptr,
          .magFilter = VK_FILTER_NEAREST,
          .minFilter = VK_FILTER_NEAREST,
          .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
          .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
          .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
          .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
          .mipLodBias = 0.0f,
          .maxAnisotropy = 1,
          .compareOp = VK_COMPARE_OP_NEVER,
          .minLod = 0.0f,
          .maxLod = 0.0f,
          .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
          .unnormalizedCoordinates = VK_FALSE,
  };
  CALL_VK(vkCreateSampler(deviceInfo.device, &sampler, nullptr,
                          &externalTextureInfo.sampler))

  // Create a pool of command buffers to allocate command buffer from
  VkCommandPoolCreateInfo cmdPoolCreateInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .pNext = nullptr,
          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
          .queueFamilyIndex = 0,
  };
  CALL_VK(vkCreateCommandPool(deviceInfo.device, &cmdPoolCreateInfo, nullptr,
                              &renderInfo.cmdPool))

  // Record a command buffer that just clear the screen
  // 1 command buffer draw in 1 framebuffer
  // In our case we need 2 command as we have 2 framebuffer
  renderInfo.cmdBufferLen = swapchainInfo.swapchainLength;
  renderInfo.cmdBuffer = new VkCommandBuffer[swapchainInfo.swapchainLength];
  VkCommandBufferAllocateInfo cmdBufferCreateInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .pNext = nullptr,
          .commandPool = renderInfo.cmdPool,
          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = renderInfo.cmdBufferLen,
  };
  CALL_VK(vkAllocateCommandBuffers(deviceInfo.device, &cmdBufferCreateInfo,
                                   renderInfo.cmdBuffer))
  // We need to create a fence to be able, in the main loop, to wait for our
  // draw command(s) to finish before swapping the framebuffers
  VkFenceCreateInfo fenceCreateInfo{
          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
  };
  CALL_VK(vkCreateFence(deviceInfo.device, &fenceCreateInfo, nullptr, &renderInfo.fence))

  // We need to create a semaphore to be able to wait, in the main loop, for our
  // framebuffer to be available for us before drawing.
  VkSemaphoreCreateInfo semaphoreCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
  };
  CALL_VK(vkCreateSemaphore(deviceInfo.device, &semaphoreCreateInfo, nullptr,
                            &renderInfo.semaphore))
  LOGI("<-createOtherStaff");
}

void VulkanRenderer::renderImpl() {
  uint32_t nextIndex;
  // Get the framebuffer index we should draw in
  auto result = vkAcquireNextImageKHR(deviceInfo.device, swapchainInfo.swapchain,
                                      UINT64_MAX, renderInfo.semaphore, VK_NULL_HANDLE,
                                      &nextIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    LOGW("vkAcquireNextImageKHR returned %i; swapchain will be recreated", result);
    cleanupSwapChain();
    createSwapChain();
    createFrameBuffersAndImages();
    return;
  }
  CALL_VK(vkResetFences(deviceInfo.device, 1, &renderInfo.fence))
  VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info = {
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .pNext = nullptr,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &renderInfo.semaphore,
          .pWaitDstStageMask = &waitStageMask,
          .commandBufferCount = 1,
          .pCommandBuffers = &renderInfo.cmdBuffer[nextIndex],
          .signalSemaphoreCount = 0,
          .pSignalSemaphores = nullptr};
  CALL_VK(vkQueueSubmit(deviceInfo.queue, 1, &submit_info, renderInfo.fence))
  LOGI("Queue submitted, waiting for a fence...");
  CALL_VK(vkWaitForFences(deviceInfo.device, 1, &renderInfo.fence, VK_TRUE, 100000000))
  LOGI("Fence signaled, presenting a frame!");
  VkPresentInfoKHR presentInfo{
          .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .pNext = nullptr,
          .waitSemaphoreCount = 0,
          .pWaitSemaphores = nullptr,
          .swapchainCount = 1,
          .pSwapchains = &swapchainInfo.swapchain,
          .pImageIndices = &nextIndex,
          .pResults = nullptr,
  };
  vkQueuePresentKHR(deviceInfo.queue, &presentInfo);
}

void VulkanRenderer::createDescriptorSet() {
  LOGI("->createDescriptorSet");
  const VkDescriptorPoolSize poolSizeUbo = {
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1
  };
  const VkDescriptorPoolSize poolSizeSampler = {
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
  };
  const auto poolSizes = new VkDescriptorPoolSize[2];
  poolSizes[0] = poolSizeUbo;
  poolSizes[1] = poolSizeSampler;
  const VkDescriptorPoolCreateInfo poolCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .pNext = nullptr,
          .maxSets = 1,
          .poolSizeCount = 2,
          .pPoolSizes = poolSizes,
  };
  CALL_VK(vkCreateDescriptorPool(deviceInfo.device, &poolCreateInfo, nullptr,
                                 &gfxPipelineInfo.descPool))
  delete[] poolSizes;

  VkDescriptorSetAllocateInfo alloc_info{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .pNext = nullptr,
          .descriptorPool = gfxPipelineInfo.descPool,
          .descriptorSetCount = 1,
          .pSetLayouts = &gfxPipelineInfo.dscLayout};
  CALL_VK(vkAllocateDescriptorSets(deviceInfo.device, &alloc_info,
                                   &gfxPipelineInfo.descSet))
  gfxPipelineInfo.descWrites = new VkWriteDescriptorSet[2];
  LOGI("<-createDescriptorSet");
}

void VulkanRenderer::hwBufferToTexture(AHardwareBuffer *buffer) {
  if (!deviceInfo.initialized) {
    return;
  }
  VkAndroidHardwareBufferFormatPropertiesANDROID ahb_format_props = {
          .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
          .pNext = nullptr,
  };
  VkAndroidHardwareBufferPropertiesANDROID ahb_props = {
          .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
          .pNext = &ahb_format_props
  };
  static auto vkGetAndroidHardwareBufferPropertiesANDROID =
          (PFN_vkGetAndroidHardwareBufferPropertiesANDROID) vkGetInstanceProcAddr(
                  deviceInfo.instance, "vkGetAndroidHardwareBufferPropertiesANDROID");
  CALL_VK(vkGetAndroidHardwareBufferPropertiesANDROID(deviceInfo.device, buffer, &ahb_props))

  VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
          .image = VK_NULL_HANDLE, // will be set later
          .buffer = VK_NULL_HANDLE,
  };

  VkImportAndroidHardwareBufferInfoANDROID importBufferInfo = {
          .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
          .pNext = &dedicatedAllocateInfo,
          .buffer = buffer,
  };

  VkMemoryAllocateInfo allocInfo{
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = &importBufferInfo,
          .allocationSize = ahb_props.allocationSize,
          .memoryTypeIndex = 0,  // Memory type assigned in the next step
  };
  mapMemoryTypeToIndex(ahb_props.memoryTypeBits,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &allocInfo.memoryTypeIndex);
  VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
          .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
  };
  AHardwareBuffer_Desc hardwareBufferDesc;
  AHardwareBuffer_describe(buffer, &hardwareBufferDesc);
  VkImageCreateInfo image_create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .pNext = &externalMemoryImageCreateInfo,
          .flags = 0,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = VK_FORMAT_R8G8B8A8_UNORM,
          .extent = {
                  static_cast<uint32_t>(hardwareBufferDesc.width),
                  static_cast<uint32_t>(hardwareBufferDesc.height),
                  1
          },
          .mipLevels = 1,
          .arrayLayers = 1,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .tiling = VK_IMAGE_TILING_OPTIMAL,
          .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 1,
          .pQueueFamilyIndices = &deviceInfo.queueFamilyIndex,
          // VK_IMAGE_LAYOUT_UNDEFINED is mandatory when using external memory
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  if (cameraInitialized) {
    vkDestroyImage(deviceInfo.device, externalTextureInfo.image, nullptr);
    vkDestroyImageView(deviceInfo.device, externalTextureInfo.view, nullptr);
    vkFreeMemory(deviceInfo.device, externalTextureInfo.memory, nullptr);
  }
  CALL_VK(vkCreateImage(deviceInfo.device, &image_create_info, nullptr,
                        &externalTextureInfo.image))
  dedicatedAllocateInfo.image = externalTextureInfo.image;
  CALL_VK(vkAllocateMemory(deviceInfo.device, &allocInfo, nullptr, &externalTextureInfo.memory))
  CALL_VK(vkBindImageMemory(deviceInfo.device, externalTextureInfo.image,
                            externalTextureInfo.memory, 0))
  VkImageViewCreateInfo view = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .image = externalTextureInfo.image,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = VK_FORMAT_R8G8B8A8_UNORM,
          .components =
                  {
                          VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                          VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
                  },
          .subresourceRange = {
                  VK_IMAGE_ASPECT_COLOR_BIT,
                  0,
                  1,
                  0,
                  1
          },
  };
  CALL_VK(vkCreateImageView(deviceInfo.device, &view, nullptr, &externalTextureInfo.view))
  VkDescriptorImageInfo imageInfo = {
          .sampler = externalTextureInfo.sampler,
          .imageView = externalTextureInfo.view,
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  VkDescriptorBufferInfo bufferInfo = {
          .buffer = buffersInfo.uniformBuf,
          .offset = 0,
          .range = sizeof(UniformBufferObject)
  };
  VkWriteDescriptorSet bufferWrite = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = gfxPipelineInfo.descSet,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pImageInfo = nullptr,
          .pBufferInfo = &bufferInfo,
          .pTexelBufferView = nullptr
  };
  VkWriteDescriptorSet imageWrite = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = gfxPipelineInfo.descSet,
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &imageInfo,
          .pBufferInfo = nullptr,
          .pTexelBufferView = nullptr};
  gfxPipelineInfo.descWrites[0] = bufferWrite;
  gfxPipelineInfo.descWrites[1] = imageWrite;
  vkUpdateDescriptorSets(deviceInfo.device, 2, gfxPipelineInfo.descWrites, 0, nullptr);
  recordCommandBuffer();
  cameraInitialized = true;
}

void VulkanRenderer::recordCommandBuffer() {
  for (int bufferIndex = 0; bufferIndex < swapchainInfo.swapchainLength; bufferIndex++) {
    // We start by creating and declare the "beginning" our command buffer
    VkCommandBufferBeginInfo cmdBufferBeginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pInheritanceInfo = nullptr,
    };
    CALL_VK(vkBeginCommandBuffer(renderInfo.cmdBuffer[bufferIndex],
                                 &cmdBufferBeginInfo))

    setImageLayout(renderInfo.cmdBuffer[bufferIndex],
                   externalTextureInfo.image,
                   VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_HOST_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    // Now we start a renderpass. Any draw command has to be recorded in a
    // renderpass
    VkClearValue clearVals{
            .color {.float32 {0.9f, 0.3f, 0.0f, 1.0f,}},
    };

    VkRenderPassBeginInfo renderPassBeginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = renderInfo.renderPass,
            .framebuffer = swapchainInfo.framebuffers[bufferIndex],
            .renderArea = {.offset =
                    {
                            .x = 0, .y = 0,
                    },
                    .extent = swapchainInfo.displaySize},
            .clearValueCount = 1,
            .pClearValues = &clearVals};
    vkCmdBeginRenderPass(renderInfo.cmdBuffer[bufferIndex], &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    // Bind what is necessary to the command buffer
    vkCmdBindPipeline(renderInfo.cmdBuffer[bufferIndex],
                      VK_PIPELINE_BIND_POINT_GRAPHICS, gfxPipelineInfo.pipeline);
    // As we support dynamic state for viewport and scissor - we must set them here
    auto viewport = VkViewport{
            .x = .0f,
            .y = .1f,
            .width = (float) swapchainInfo.displaySize.width,
            .height = (float) swapchainInfo.displaySize.height,
            .minDepth = .0f,
            .maxDepth = .1f,
    };
    vkCmdSetViewport(renderInfo.cmdBuffer[bufferIndex], 0, 1, &viewport);
    auto scissor = VkRect2D{
            .offset = {0, 0},
            .extent = swapchainInfo.displaySize,
    };
    vkCmdSetScissor(renderInfo.cmdBuffer[bufferIndex], 0, 1, &scissor);
    vkCmdBindDescriptorSets(
            renderInfo.cmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
            gfxPipelineInfo.layout, 0, 1, &gfxPipelineInfo.descSet, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(renderInfo.cmdBuffer[bufferIndex], 0, 1,
                           &buffersInfo.vertexBuf, &offset);

    vkCmdDraw(renderInfo.cmdBuffer[bufferIndex], 4, 1, 0, 0);
    vkCmdEndRenderPass(renderInfo.cmdBuffer[bufferIndex]);
    CALL_VK(vkEndCommandBuffer(renderInfo.cmdBuffer[bufferIndex]))
  }
}

void VulkanRenderer::onMvpUpdated() {
  UniformBufferObject ubo{};
  ubo.mvp = mvp;
  memcpy(buffersInfo.uniformBufferMapped, &ubo, sizeof(ubo));
  LOGI("MVP updated");
}

void VulkanRenderer::createVertexBuffer() {
  const float vertexData[] = {
          -1.0f, -1.0f, 0.0f, 0.0f,
          1.0f, -1.0f, 1.0f, 0.0f,
          -1.0f, 1.0f, 0.0f, 1.0f,
          1.0f, 1.0f, 1.0f, 1.0f,
  };
  createBuffer(
          sizeof(vertexData),
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          buffersInfo.vertexBuf,
          buffersInfo.vertexBufferMemory
  );

  void *data;
  CALL_VK(vkMapMemory(deviceInfo.device, buffersInfo.vertexBufferMemory, 0, sizeof(vertexData),
                      0, &data))
  memcpy(data, vertexData, sizeof(vertexData));
  vkUnmapMemory(deviceInfo.device, buffersInfo.vertexBufferMemory);
}

void VulkanRenderer::cleanupSwapChain() const {
  LOGI("->cleanupSwapChain");
  for (int i = 0; i < swapchainInfo.swapchainLength; ++i) {
    vkDestroyFramebuffer(deviceInfo.device, swapchainInfo.framebuffers[i], nullptr);
    vkDestroyImageView(deviceInfo.device, swapchainInfo.displayViews[i], nullptr);
  }
  vkDestroySwapchainKHR(deviceInfo.device, swapchainInfo.swapchain, nullptr);
  LOGI("<-cleanupSwapChain");
}

void VulkanRenderer::cleanup() {
  if (!deviceInfo.initialized) {
    LOGI("Cleanup called but Vulkan was not initialized.");
    return;
  }
  LOGI("->cleanup");
  cleanupSwapChain();
  vkDestroyPipeline(deviceInfo.device, gfxPipelineInfo.pipeline, nullptr);
  vkDestroyPipelineLayout(deviceInfo.device, gfxPipelineInfo.layout, nullptr);
  vkDestroyPipelineCache(deviceInfo.device, gfxPipelineInfo.cache, nullptr);
  vkDestroyRenderPass(deviceInfo.device, renderInfo.renderPass, nullptr);
  vkDestroySemaphore(deviceInfo.device, renderInfo.semaphore, nullptr);
  vkDestroyFence(deviceInfo.device, renderInfo.fence, nullptr);
  vkDestroyCommandPool(deviceInfo.device, renderInfo.cmdPool, nullptr);
  vkDestroySampler(deviceInfo.device, externalTextureInfo.sampler, nullptr);
  if (cameraInitialized) {
    vkDestroyImage(deviceInfo.device, externalTextureInfo.image, nullptr);
    vkDestroyImageView(deviceInfo.device, externalTextureInfo.view, nullptr);
    vkFreeMemory(deviceInfo.device, externalTextureInfo.memory, nullptr);
  }
  vkDestroyDescriptorSetLayout(deviceInfo.device, gfxPipelineInfo.dscLayout, nullptr);
  vkDestroyDescriptorPool(deviceInfo.device, gfxPipelineInfo.descPool, nullptr);
  vkDestroyBuffer(deviceInfo.device, buffersInfo.uniformBuf, nullptr);
  vkDestroyBuffer(deviceInfo.device, buffersInfo.vertexBuf, nullptr);
  vkFreeMemory(deviceInfo.device, buffersInfo.uniformBufferMemory, nullptr);
  vkFreeMemory(deviceInfo.device, buffersInfo.vertexBufferMemory, nullptr);
  vkDestroyDevice(deviceInfo.device, nullptr);
  vkDestroySurfaceKHR(deviceInfo.instance, deviceInfo.surface, nullptr);
  vkDestroyInstance(deviceInfo.instance, nullptr);
  LOGI("<-cleanup");
}

} // namespace android
} // namespace engine
