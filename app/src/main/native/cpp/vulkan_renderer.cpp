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
  // TODO add them to debug build only!
  std::vector<const char*> validation_layers;

  instance_extensions.push_back("VK_KHR_surface");
  instance_extensions.push_back("VK_KHR_android_surface");

  device_extensions.push_back("VK_KHR_swapchain");
  device_extensions.push_back("VK_ANDROID_external_memory_android_hardware_buffer");
  device_extensions.push_back("VK_EXT_queue_family_foreign");

  validation_layers.push_back("VK_LAYER_KHRONOS_validation");

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
          .compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
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

void VulkanRenderer::createTexture() {
  LOGI("->createTexture");

  // Check for linear supportability
  VkFormatProperties props;
  vkGetPhysicalDeviceFormatProperties(device.gpuDevice_, kTexFmt, &props);
  assert((props.linearTilingFeatures | props.optimalTilingFeatures) &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

  if (props.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
    // linear format supporting the required texture
  } else {
    throw std::exception();
  }
  VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
          .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID
  };
  // Allocate the linear texture so texture could be copied over
  VkImageCreateInfo image_create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .pNext = &externalMemoryImageCreateInfo,
          .flags = 0,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = kTexFmt,
          .extent = {static_cast<uint32_t>(swapchain.displaySize_.width),
                     static_cast<uint32_t>(swapchain.displaySize_.height), 1},
          .mipLevels = 1,
          .arrayLayers = 1,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .tiling = VK_IMAGE_TILING_LINEAR,
          .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 1,
          .pQueueFamilyIndices = &device.queueFamilyIndex_,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  CALL_VK(vkCreateImage(device.device_, &image_create_info, nullptr,
                        &tex_obj.image));
  tex_obj.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

//  VkCommandPoolCreateInfo cmdPoolCreateInfo{
//          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
//          .pNext = nullptr,
//          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
//          .queueFamilyIndex = device.queueFamilyIndex_,
//  };
//
//  VkCommandPool cmdPool;
//  CALL_VK(vkCreateCommandPool(device.device_, &cmdPoolCreateInfo, nullptr,
//                              &cmdPool));
//
//  VkCommandBuffer gfxCmd;
//  const VkCommandBufferAllocateInfo cmd = {
//          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
//          .pNext = nullptr,
//          .commandPool = cmdPool,
//          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
//          .commandBufferCount = 1,
//  };
//
//  CALL_VK(vkAllocateCommandBuffers(device.device_, &cmd, &gfxCmd));
//  VkCommandBufferBeginInfo cmd_buf_info = {
//          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
//          .pNext = nullptr,
//          .flags = 0,
//          .pInheritanceInfo = nullptr};
//  CALL_VK(vkBeginCommandBuffer(gfxCmd, &cmd_buf_info));

//  setImageLayout(gfxCmd, tex_obj.image, VK_IMAGE_LAYOUT_UNDEFINED,
//                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
//                 VK_PIPELINE_STAGE_HOST_BIT,
//                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

//  CALL_VK(vkEndCommandBuffer(gfxCmd));
//  VkFenceCreateInfo fenceInfo = {
//          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
//          .pNext = nullptr,
//          .flags = 0,
//  };
//  VkFence fence;
//  CALL_VK(vkCreateFence(device.device_, &fenceInfo, nullptr, &fence));
//
//  VkSubmitInfo submitInfo = {
//          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//          .pNext = nullptr,
//          .waitSemaphoreCount = 0,
//          .pWaitSemaphores = nullptr,
//          .pWaitDstStageMask = nullptr,
//          .commandBufferCount = 1,
//          .pCommandBuffers = &gfxCmd,
//          .signalSemaphoreCount = 0,
//          .pSignalSemaphores = nullptr,
//  };
//  CALL_VK(vkQueueSubmit(device.queue_, 1, &submitInfo, fence) != VK_SUCCESS);
//  CALL_VK(vkWaitForFences(device.device_, 1, &fence, VK_TRUE, 100000000) !=
//          VK_SUCCESS);
//  vkDestroyFence(device.device_, fence, nullptr);
//
//  vkFreeCommandBuffers(device.device_, cmdPool, 1, &gfxCmd);
//  vkDestroyCommandPool(device.device_, cmdPool, nullptr);

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
  CALL_VK(vkCreateSampler(device.device_, &sampler, nullptr,
                          &tex_obj.sampler));
//  auto code = vkCreateImageView(device.device_, &view, nullptr, &tex_obj.view);
//  LOGI("<-createTexture, %d", code);
  LOGI("<-createTexture");
}

void VulkanRenderer::createBuffers() {
  // Vertex positions
  const float vertexData[] = {
          -1.0f, -1.0f, 0.0f,0.0f,
          1.0f, -1.0f, 1.0f,  0.0f,
          -1.0f, 1.0f,0.0f, 1.0f,
          1.0f, 1.0f, 1.0f, 1.0f,
  };

  // Create a vertex buffer
  VkBufferCreateInfo createBufferInfo{
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .size = sizeof(vertexData),
          .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 1,
          .pQueueFamilyIndices = &device.queueFamilyIndex_,
  };

  CALL_VK(vkCreateBuffer(device.device_, &createBufferInfo, nullptr,
                         &buffers.vertexBuf_));

  VkMemoryRequirements memReq;
  vkGetBufferMemoryRequirements(device.device_, buffers.vertexBuf_, &memReq);

  VkMemoryAllocateInfo allocInfo{
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = nullptr,
          .allocationSize = memReq.size,
          .memoryTypeIndex = 0,  // Memory type assigned in the next step
  };

  // Assign the proper memory type for that buffer
  mapMemoryTypeToIndex(memReq.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &allocInfo.memoryTypeIndex);

  // Allocate memory for the buffer
  VkDeviceMemory deviceMemory;
  CALL_VK(vkAllocateMemory(device.device_, &allocInfo, nullptr, &deviceMemory));

  void* data;
  CALL_VK(vkMapMemory(device.device_, deviceMemory, 0, allocInfo.allocationSize,
                      0, &data));
  memcpy(data, vertexData, sizeof(vertexData));
  vkUnmapMemory(device.device_, deviceMemory);

  CALL_VK(vkBindBufferMemory(device.device_, buffers.vertexBuf_, deviceMemory, 0));
}

void VulkanRenderer::mapMemoryTypeToIndex(uint32_t typeBits,
                                          VkFlags requirements_mask,
                                          uint32_t *typeIndex) {
  VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(device.gpuDevice_, &memoryProperties);
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

void VulkanRenderer::putAllTogether() {
  LOGI("->putAllTogether");
  // Create a pool of command buffers to allocate command buffer from
  VkCommandPoolCreateInfo cmdPoolCreateInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .pNext = nullptr,
          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
          .queueFamilyIndex = 0,
  };
  CALL_VK(vkCreateCommandPool(device.device_, &cmdPoolCreateInfo, nullptr,
                              &renderInfo.cmdPool_));

  // Record a command buffer that just clear the screen
  // 1 command buffer draw in 1 framebuffer
  // In our case we need 2 command as we have 2 framebuffer
  renderInfo.cmdBufferLen_ = swapchain.swapchainLength_;
  renderInfo.cmdBuffer_ = new VkCommandBuffer[swapchain.swapchainLength_];
  VkCommandBufferAllocateInfo cmdBufferCreateInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .pNext = nullptr,
          .commandPool = renderInfo.cmdPool_,
          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = renderInfo.cmdBufferLen_,
  };
  CALL_VK(vkAllocateCommandBuffers(device.device_, &cmdBufferCreateInfo,
                                   renderInfo.cmdBuffer_));

  for (int bufferIndex = 0; bufferIndex < swapchain.swapchainLength_; bufferIndex++) {
    // We start by creating and declare the "beginning" our command buffer
    VkCommandBufferBeginInfo cmdBufferBeginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pInheritanceInfo = nullptr,
    };
    CALL_VK(vkBeginCommandBuffer(renderInfo.cmdBuffer_[bufferIndex],
                                 &cmdBufferBeginInfo));

    // transition the buffer into color attachment
    setImageLayout(renderInfo.cmdBuffer_[bufferIndex],
                   swapchain.displayImages_[bufferIndex],
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    // Now we start a renderpass. Any draw command has to be recorded in a
    // renderpass
    VkClearValue clearVals{
            .color { .float32 { 0.0f, 0.34f, 0.90f, 1.0f,}},
    };

    VkRenderPassBeginInfo renderPassBeginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = renderInfo.renderPass_,
            .framebuffer = swapchain.framebuffers_[bufferIndex],
            .renderArea = {.offset =
                    {
                            .x = 0, .y = 0,
                    },
                    .extent = swapchain.displaySize_},
            .clearValueCount = 1,
            .pClearValues = &clearVals};
    vkCmdBeginRenderPass(renderInfo.cmdBuffer_[bufferIndex], &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    // Bind what is necessary to the command buffer
    vkCmdBindPipeline(renderInfo.cmdBuffer_[bufferIndex],
                      VK_PIPELINE_BIND_POINT_GRAPHICS, gfxPipeline.pipeline_);
    vkCmdBindDescriptorSets(
            renderInfo.cmdBuffer_[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
            gfxPipeline.layout_, 0, 1, &gfxPipeline.descSet_, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(renderInfo.cmdBuffer_[bufferIndex], 0, 1,
                           &buffers.vertexBuf_, &offset);

    vkCmdDraw(renderInfo.cmdBuffer_[bufferIndex], 4, 1, 0, 0);
    vkCmdEndRenderPass(renderInfo.cmdBuffer_[bufferIndex]);
    setImageLayout(renderInfo.cmdBuffer_[bufferIndex],
                   swapchain.displayImages_[bufferIndex],
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    CALL_VK(vkEndCommandBuffer(renderInfo.cmdBuffer_[bufferIndex]));
  }

  // We need to create a fence to be able, in the main loop, to wait for our
  // draw command(s) to finish before swapping the framebuffers
  VkFenceCreateInfo fenceCreateInfo{
          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
  };
  CALL_VK(vkCreateFence(device.device_, &fenceCreateInfo, nullptr, &renderInfo.fence_));

  // We need to create a semaphore to be able to wait, in the main loop, for our
  // framebuffer to be available for us before drawing.
  VkSemaphoreCreateInfo semaphoreCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
  };
  CALL_VK(vkCreateSemaphore(device.device_, &semaphoreCreateInfo, nullptr,&renderInfo.semaphore_));
  LOGI("<-putAllTogether");
}

void VulkanRenderer::renderImpl() {
  uint32_t nextIndex;
  // Get the framebuffer index we should draw in
  CALL_VK(vkAcquireNextImageKHR(device.device_, swapchain.swapchain_,
                                UINT64_MAX, renderInfo.semaphore_, VK_NULL_HANDLE,
                                &nextIndex));
  CALL_VK(vkResetFences(device.device_, 1, &renderInfo.fence_));

  VkPipelineStageFlags waitStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .pNext = nullptr,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &renderInfo.semaphore_,
          .pWaitDstStageMask = &waitStageMask,
          .commandBufferCount = 1,
          .pCommandBuffers = &renderInfo.cmdBuffer_[nextIndex],
          .signalSemaphoreCount = 0,
          .pSignalSemaphores = nullptr};
  CALL_VK(vkQueueSubmit(device.queue_, 1, &submit_info, renderInfo.fence_));
  CALL_VK(vkWaitForFences(device.device_, 1, &renderInfo.fence_, VK_TRUE, 100000000));

  LOGI("Drawing frames......");

  VkResult result;
  VkPresentInfoKHR presentInfo{
          .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .pNext = nullptr,
          .waitSemaphoreCount = 0,
          .pWaitSemaphores = nullptr,
          .swapchainCount = 1,
          .pSwapchains = &swapchain.swapchain_,
          .pImageIndices = &nextIndex,
          .pResults = &result,
  };
  vkQueuePresentKHR(device.queue_, &presentInfo);
}

void VulkanRenderer::createDescriptorSet() {
  LOGI("->createDescriptorSet");
  const VkDescriptorPoolSize type_count = {
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
  };
  const VkDescriptorPoolCreateInfo descriptor_pool = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .pNext = nullptr,
          .maxSets = 1,
          .poolSizeCount = 1,
          .pPoolSizes = &type_count,
  };

  CALL_VK(vkCreateDescriptorPool(device.device_, &descriptor_pool, nullptr,
                                 &gfxPipeline.descPool_));

  VkDescriptorSetAllocateInfo alloc_info{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .pNext = nullptr,
          .descriptorPool = gfxPipeline.descPool_,
          .descriptorSetCount = 1,
          .pSetLayouts = &gfxPipeline.dscLayout_};
  CALL_VK(vkAllocateDescriptorSets(device.device_, &alloc_info,
                                   &gfxPipeline.descSet_));

  VkDescriptorImageInfo texDst;

  texDst.sampler = tex_obj.sampler;
  texDst.imageView = tex_obj.view;
  // changed from VK_IMAGE_LAYOUT_GENERAL to avoid validation complaining
  texDst.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet writeDst{
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .pNext = nullptr,
          .dstSet = gfxPipeline.descSet_,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &texDst,
          .pBufferInfo = nullptr,
          .pTexelBufferView = nullptr};
  vkUpdateDescriptorSets(device.device_, 1, &writeDst, 0, nullptr);
  LOGI("<-createDescriptorSet");
}

void VulkanRenderer::hwBufferToTexture(AHardwareBuffer *buffer) {
  // Get the AHardwareBuffer properties
  VkAndroidHardwareBufferFormatPropertiesANDROID ahb_format_props = {
          .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
          .pNext = nullptr,
  };
  VkAndroidHardwareBufferPropertiesANDROID ahb_props = {
          .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
          .pNext = &ahb_format_props
  };
  AHardwareBuffer_Desc description;
  AHardwareBuffer_describe(buffer, &description);

  LOGI("Usage native: %llu", description.usage);
  LOGI("Format native: %u", description.format);
  LOGI("Dimensions native: %u, %u", description.width, description.height);
  LOGI("Image data native: stride=%u, layers=%u", description.stride, description.layers);

  auto vkGetAndroidHardwareBufferPropertiesANDROID =
          (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetInstanceProcAddr(device.instance_, "vkGetAndroidHardwareBufferPropertiesANDROID");
  CALL_VK(vkGetAndroidHardwareBufferPropertiesANDROID(device.device_, buffer, &ahb_props))

  // Import the AHardwareBuffer as Vulkan memory
  VkImportAndroidHardwareBufferInfoANDROID importBufferInfo = {
          .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
          .buffer = buffer,
  };

  VkMemoryAllocateInfo allocInfo{
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = &importBufferInfo,
          .allocationSize = ahb_props.allocationSize,
          .memoryTypeIndex = 0,  // Memory type assigned in the next step
  };

  // Assign the proper memory type for that buffer
  mapMemoryTypeToIndex(ahb_props.memoryTypeBits,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &allocInfo.memoryTypeIndex);

  VkDeviceMemory deviceMemory;
  if (!device.textureDataInitialized_) {
    createTexture();
    CALL_VK(vkAllocateMemory(device.device_, &allocInfo, nullptr, &deviceMemory))
    CALL_VK(vkBindImageMemory(device.device_, tex_obj.image, deviceMemory, 0))
    VkImageViewCreateInfo view = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = tex_obj.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = kTexFmt,
            .components =
                    {
                            VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                            VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
                    },
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    CALL_VK(vkCreateImageView(device.device_, &view, nullptr, &tex_obj.view))
    createBuffers();
    createGraphicsPipeline();
    createDescriptorSet();
    putAllTogether();
    device.textureDataInitialized_ = true;
  } else {
    CALL_VK(vkAllocateMemory(device.device_, &allocInfo, nullptr, &deviceMemory))
    CALL_VK(vkBindImageMemory(device.device_, tex_obj.image, deviceMemory, 0))
  }
}

} // namespace android
} // namespace engine
