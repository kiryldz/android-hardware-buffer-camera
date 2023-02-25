#include "vulkan_renderer.hpp"

#define VOLK_IMPLEMENTATION
#include <volk.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace engine {
namespace android {

bool validate_extensions(const std::vector<const char *>            &required,
                                           const std::vector<vk::ExtensionProperties> &available) {
	// inner find_if gives true if the extension was not found
	// outer find_if gives true if none of the extensions were not found, that is if all extensions were found
	return std::find_if(required.begin(),
	                    required.end(),
	                    [&available](auto extension) {
		                    return std::find_if(available.begin(),
		                                        available.end(),
		                                        [&extension](auto const &ep) {
			                                        return strcmp(ep.extensionName, extension) == 0;
		                                        }) == available.end();
	                    }) == required.end();
}

VkSurfaceKHR createSurface(vk::Instance instance, ANativeWindow* window) {
	VkAndroidSurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	createInfo.window = window;

	VkSurfaceKHR surface;
	const auto result = static_cast<vk::Result>(vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &surface));
	if (result != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to create surface");
	}

	return surface;
}

void VulkanRenderer::init_instance(Context &context,
                                   const std::vector<const char *> &required_instance_extensions,
                                   const std::vector<const char *> &required_validation_layers) {
	LOGI("Initializing Vulkan instance (but Volk is loaded first of all!)");

	if (volkInitialize() != VkResult::VK_SUCCESS) {
		LOGE("Impossible to work w/o VOLK, uf-uf!");
	}

	static vk::DynamicLoader dl;
	auto vkGetInstanceProcAddr =
		dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

	std::vector<vk::ExtensionProperties> instance_extensions = vk::enumerateInstanceExtensionProperties();

	std::vector<const char *> active_instance_extensions(required_instance_extensions);

	// could simplify extension check as we target Android only
	active_instance_extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);

	if (!validate_extensions(active_instance_extensions, instance_extensions)) {
		throw std::runtime_error("Required instance extensions are missing.");
	}

	std::vector<vk::LayerProperties> supported_validation_layers = vk::enumerateInstanceLayerProperties();
	std::vector<const char *> requested_validation_layers(required_validation_layers);

	vk::ApplicationInfo app("DzCameraFast", {}, "engine", VK_MAKE_VERSION(1, 0, 0));
	vk::InstanceCreateInfo instance_info({}, &app, requested_validation_layers,
	                                     active_instance_extensions);

	// Create the Vulkan instance
	context.instance = vk::createInstance(instance_info);

	// UF-UF
	volkLoadInstance(context.instance);

	// initialize function pointers for instance
	VULKAN_HPP_DEFAULT_DISPATCHER.init(context.instance);
}

void VulkanRenderer::select_physical_device_and_surface(Context &context) {
	std::vector<vk::PhysicalDevice> gpus = context.instance.enumeratePhysicalDevices();

	for (size_t i = 0; i < gpus.size() && (context.graphics_queue_index < 0); i++) {
		context.gpu = gpus[i];

		std::vector<vk::QueueFamilyProperties> queue_family_properties = context.gpu.getQueueFamilyProperties();

		if (queue_family_properties.empty()) {
			throw std::runtime_error("No queue family found.");
		}

		if (context.surface) {
			context.instance.destroySurfaceKHR(context.surface);
		}

		vk::SurfaceKHR vkSurfaceKHR(createSurface(context.instance, aNativeWindow));
		context.surface = vkSurfaceKHR;
		if (!context.surface) {
			throw std::runtime_error("Failed to create window surface.");
		}

		for (uint32_t j = 0; j < static_cast<uint32_t>(queue_family_properties.size()); j++) {
			vk::Bool32 supports_present = context.gpu.getSurfaceSupportKHR(j, context.surface);

			// Find a queue family which supports graphics and presentation.
			if ((queue_family_properties[j].queueFlags & vk::QueueFlagBits::eGraphics) && supports_present) {
				context.graphics_queue_index = static_cast<int32_t>(j);
				break;
			}
		}
	}

	if (context.graphics_queue_index < 0) {
		LOGE("Did not find suitable queue which supports graphics and presentation.");
	}
}

void VulkanRenderer::init_device(Context &context,
                                 const std::vector<const char *> &required_device_extensions) {
	LOGI("Initializing Vulkan device.");

	std::vector<vk::ExtensionProperties> device_extensions = context.gpu.enumerateDeviceExtensionProperties();

	if (!validate_extensions(required_device_extensions, device_extensions)) {
		throw std::runtime_error("Required device extensions are missing, will try without.");
	}

	float queue_priority = 1.0f;

	// Create one queue
	vk::DeviceQueueCreateInfo queue_info({}, context.graphics_queue_index, 1, &queue_priority);

	vk::DeviceCreateInfo device_info({}, queue_info, {}, required_device_extensions);

	context.device = context.gpu.createDevice(device_info);
	// initialize function pointers for device
	VULKAN_HPP_DEFAULT_DISPATCHER.init(context.device);

	context.queue = context.device.getQueue(context.graphics_queue_index, 0);

	// uf-uf
	volkLoadDevice(context.device);
}

void VulkanRenderer::init_per_frame(Context &context, PerFrame &per_frame) {
	per_frame.queue_submit_fence = context.device.createFence({vk::FenceCreateFlagBits::eSignaled});

	vk::CommandPoolCreateInfo cmd_pool_info(vk::CommandPoolCreateFlagBits::eTransient, context.graphics_queue_index);
	per_frame.primary_command_pool = context.device.createCommandPool(cmd_pool_info);

	vk::CommandBufferAllocateInfo cmd_buf_info(per_frame.primary_command_pool, vk::CommandBufferLevel::ePrimary, 1);
	per_frame.primary_command_buffer = context.device.allocateCommandBuffers(cmd_buf_info).front();

	per_frame.device      = context.device;
	per_frame.queue_index = context.graphics_queue_index;
}

void VulkanRenderer::init_swapchain(Context &context) {
	vk::SurfaceCapabilitiesKHR surface_properties = context.gpu.getSurfaceCapabilitiesKHR(context.surface);

	std::vector<vk::SurfaceFormatKHR> formats = context.gpu.getSurfaceFormatsKHR(context.surface);

	// always require R8G8B8A8 format for simplicity
	vk::SurfaceFormatKHR format = vk::Format::eUndefined;
	for (auto &candidate : formats) {
		switch (candidate.format) {
			case vk::Format::eR8G8B8A8Srgb:
				format = candidate;
				break;

			default:
				break;
		}

		if (format.format != vk::Format::eUndefined) {
			break;
		}
	}

	if (format == vk::Format::eUndefined) {
		throw std::runtime_error("Could not find R8G8B8A8 surface format");
	}

	vk::Extent2D swapchain_size;
	if (surface_properties.currentExtent.width == 0xFFFFFFFF) {
		swapchain_size.width  = context.swapchain_dimensions.width;
		swapchain_size.height = context.swapchain_dimensions.height;
	} else {
		swapchain_size = surface_properties.currentExtent;
	}

	// FIFO must be supported by all implementations.
	vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eFifo;

	// Determine the number of vk::Image's to use in the swapchain.
	// Ideally, we desire to own 1 image at a time, the rest of the images can
	// either be rendered to and/or being queued up for display.
	uint32_t desired_swapchain_images = surface_properties.minImageCount + 1;
	if ((surface_properties.maxImageCount > 0) && (desired_swapchain_images > surface_properties.maxImageCount)) {
		// Application must settle for fewer images than desired.
		desired_swapchain_images = surface_properties.maxImageCount;
	}

	// Figure out a suitable surface transform.
	vk::SurfaceTransformFlagBitsKHR pre_transform =
		(surface_properties.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) ?
		vk::SurfaceTransformFlagBitsKHR::eIdentity :
		surface_properties.currentTransform;

	vk::SwapchainKHR old_swapchain = context.swapchain;

	// Find a supported composite type.
	vk::CompositeAlphaFlagBitsKHR composite = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque) {
		composite = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	} else if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit) {
		composite = vk::CompositeAlphaFlagBitsKHR::eInherit;
	} else if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied) {
		composite = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
	} else if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied) {
		composite = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
	}

	vk::SwapchainCreateInfoKHR info;
	info.surface            = context.surface;
	info.minImageCount      = desired_swapchain_images;
	info.imageFormat        = format.format;
	info.imageColorSpace    = format.colorSpace;
	info.imageExtent.width  = swapchain_size.width;
	info.imageExtent.height = swapchain_size.height;
	info.imageArrayLayers   = 1;
	info.imageUsage         = vk::ImageUsageFlagBits::eColorAttachment;
	info.imageSharingMode   = vk::SharingMode::eExclusive;
	info.preTransform       = pre_transform;
	info.compositeAlpha     = composite;
	info.presentMode        = swapchain_present_mode;
	info.clipped            = true;
	info.oldSwapchain       = old_swapchain;

	context.swapchain = context.device.createSwapchainKHR(info);

	// TODO per current design seems it's not needed for now,
	//  we are configuring totally new swapchain
//	if (old_swapchain) {
//		for (vk::ImageView image_view : context.swapchain_image_views) {
//			context.device.destroyImageView(image_view);
//		}
//
//		size_t image_count = context.device.getSwapchainImagesKHR(old_swapchain).size();
//
//		for (size_t i = 0; i < image_count; i++)
//		{
//			teardown_per_frame(context, context.per_frame[i]);
//		}
//
//		context.swapchain_image_views.clear();
//
//		context.device.destroySwapchainKHR(old_swapchain);
//	}
//
	context.swapchain_dimensions = {swapchain_size.width,
																	swapchain_size.height,
																	format.format};

	/// The swapchain images.
	std::vector<vk::Image> swapchain_images = context.device.getSwapchainImagesKHR(context.swapchain);
	size_t                 image_count      = swapchain_images.size();

	// Initialize per-frame resources.
	// Every swapchain image has its own command pool and fence manager.
	// This makes it very easy to keep track of when we can reset command buffers and such.
	context.per_frame.clear();
	context.per_frame.resize(image_count);

	for (size_t i = 0; i < image_count; i++) {
		init_per_frame(context, context.per_frame[i]);
	}

	vk::ImageViewCreateInfo view_info;
	view_info.viewType                    = vk::ImageViewType::e2D;
	view_info.format                      = context.swapchain_dimensions.format;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	view_info.components.r                = vk::ComponentSwizzle::eR;
	view_info.components.g                = vk::ComponentSwizzle::eG;
	view_info.components.b                = vk::ComponentSwizzle::eB;
	view_info.components.a                = vk::ComponentSwizzle::eA;
	for (size_t i = 0; i < image_count; i++) {
		// Create an image view which we can render into.
		view_info.image = swapchain_images[i];
		context.swapchain_image_views.push_back(context.device.createImageView(view_info));
	}
}

void VulkanRenderer::init_render_pass(Context &context) {
	vk::AttachmentDescription attachment;
	// Backbuffer format.
	attachment.format = context.swapchain_dimensions.format;
	// Not multisampled.
	attachment.samples = vk::SampleCountFlagBits::e1;
	// When starting the frame, we want tiles to be cleared.
	attachment.loadOp = vk::AttachmentLoadOp::eClear;
	// When ending the frame, we want tiles to be written out.
	attachment.storeOp = vk::AttachmentStoreOp::eStore;
	// Don't care about stencil since we're not using it.
	attachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
	attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

	// The image layout will be undefined when the render pass begins.
	attachment.initialLayout = vk::ImageLayout::eUndefined;
	// After the render pass is complete, we will transition to ePresentSrcKHR layout.
	attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

	// We have one subpass. This subpass has one color attachment.
	// While executing this subpass, the attachment will be in attachment optimal layout.
	vk::AttachmentReference color_ref(0, vk::ImageLayout::eColorAttachmentOptimal);

	// We will end up with two transitions.
	// The first one happens right before we start subpass #0, where
	// eUndefined is transitioned into eColorAttachmentOptimal.
	// The final layout in the render pass attachment states ePresentSrcKHR, so we
	// will get a final transition from eColorAttachmentOptimal to ePresetSrcKHR.
	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, color_ref);

	// Create a dependency to external events.
	// We need to wait for the WSI semaphore to signal.
	// Only pipeline stages which depend on eColorAttachmentOutput will
	// actually wait for the semaphore, so we must also wait for that pipeline stage.
	vk::SubpassDependency dependency;
	dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass   = 0;
	dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	// Since we changed the image layout, we need to make the memory visible to
	// color attachment to modify.
	dependency.srcAccessMask = {};
	dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

	// Finally, create the renderpass.
	vk::RenderPassCreateInfo rp_info({}, attachment, subpass, dependency);

	context.render_pass = context.device.createRenderPass(rp_info);
}

vk::ShaderModule VulkanRenderer::load_shader_module(
	Context &context,
	const char *shaderCode,
	EShLanguage shaderKind
) {
	// Initialize glslang
	glslang::InitializeProcess();

	// Parse GLSL shader into IR
	glslang::TShader shader(shaderKind);

	shader.setStrings(&shaderCode, 1);

	auto messages = static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
	TBuiltInResource resources = {};
	// needed for vertex
	resources.limits.generalVariableIndexing = true;
	// needed for fragment
	resources.maxDrawBuffers = true;
	if (!shader.parse(&resources, 100, false, messages)) {
		LOGE("Error parsing %s shader: %s", shaderKind == EShLangVertex ? "vertex" : "fragment", shader.getInfoLog());
		throw std::runtime_error("Error when compiling shader");
	}

	// Convert IR to SPIR-V
	glslang::TIntermediate* intermediate = shader.getIntermediate();
	std::vector<unsigned int> spirv;
	glslang::GlslangToSpv(*intermediate, spirv);

	if (spirv.empty()) {
		throw std::runtime_error("Error when converting shader to spir-v");
	}

	// Cleanup glslang
	glslang::FinalizeProcess();
	vk::ShaderModuleCreateInfo module_info({}, spirv);
	return context.device.createShaderModule(module_info);
}

void VulkanRenderer::init_pipeline(Context &context) {
	// Create a blank pipeline layout.
	// We are not binding any resources to the pipeline in this first sample.
	context.pipeline_layout = context.device.createPipelineLayout({});

	vk::PipelineVertexInputStateCreateInfo vertex_input;

	// Specify we will use triangle lists to draw geometry.
	vk::PipelineInputAssemblyStateCreateInfo input_assembly({}, vk::PrimitiveTopology::eTriangleList);

	// Specify rasterization state.
	vk::PipelineRasterizationStateCreateInfo raster;
	raster.cullMode  = vk::CullModeFlagBits::eBack;
	raster.frontFace = vk::FrontFace::eClockwise;
	raster.lineWidth = 1.0f;

	// Our attachment will write to all color channels, but no blending is enabled.
	vk::PipelineColorBlendAttachmentState blend_attachment;
	blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
		vk::ColorComponentFlagBits::eG |
		vk::ColorComponentFlagBits::eB |
		vk::ColorComponentFlagBits::eA;

	vk::PipelineColorBlendStateCreateInfo blend({}, {}, {}, blend_attachment);

	// We will have one viewport and scissor box.
	vk::PipelineViewportStateCreateInfo viewport;
	viewport.viewportCount = 1;
	viewport.scissorCount  = 1;

	// Disable all depth testing.
	vk::PipelineDepthStencilStateCreateInfo depth_stencil;

	// No multisampling.
	vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);

	// Specify that these states will be dynamic, i.e. not part of pipeline state object.
	std::array<vk::DynamicState, 2> dynamics{vk::DynamicState::eViewport, vk::DynamicState::eScissor};

	vk::PipelineDynamicStateCreateInfo dynamic({}, dynamics);

	// Load our SPIR-V shaders.
	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages{
		vk::PipelineShaderStageCreateInfo(
			{},
			vk::ShaderStageFlagBits::eVertex,
			load_shader_module(
				context,
				vertexShader,
				EShLangVertex
			),
			"main"
		),
		vk::PipelineShaderStageCreateInfo(
			{},
			vk::ShaderStageFlagBits::eFragment,
			load_shader_module(
				context,
				fragmentShader,
				EShLangFragment
			),
			"main"
		)
	};

	vk::GraphicsPipelineCreateInfo pipe({}, shader_stages);
	pipe.pVertexInputState   = &vertex_input;
	pipe.pInputAssemblyState = &input_assembly;
	pipe.pRasterizationState = &raster;
	pipe.pColorBlendState    = &blend;
	pipe.pMultisampleState   = &multisample;
	pipe.pViewportState      = &viewport;
	pipe.pDepthStencilState  = &depth_stencil;
	pipe.pDynamicState       = &dynamic;

	// We need to specify the pipeline layout and the render pass description up front as well.
	pipe.renderPass = context.render_pass;
	pipe.layout     = context.pipeline_layout;

	vk::Result result;
	std::tie(result, context.pipeline) = context.device.createGraphicsPipeline(nullptr, pipe);
	if (result != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to create graphics pipeline.");
	}

	// Pipeline is baked, we can delete the shader modules now.
	context.device.destroyShaderModule(shader_stages[0].module);
	context.device.destroyShaderModule(shader_stages[1].module);
}

void VulkanRenderer::init_framebuffers(Context &context) {
	vk::Device device = context.device;
	// Create framebuffer for each swapchain image view
	for (auto &image_view : context.swapchain_image_views) {
		// Build the framebuffer.
		vk::FramebufferCreateInfo fb_info({}, context.render_pass, image_view, context.swapchain_dimensions.width, context.swapchain_dimensions.height, 1);
		context.swapchain_framebuffers.push_back(device.createFramebuffer(fb_info));
	}
}

void VulkanRenderer::doFrame(long, void* data) {
	auto * renderer = reinterpret_cast<engine::android::VulkanRenderer*>(data);
	if (renderer->couldRender()) {
		renderer->render();
		// perform the check if aHwBufferQueue is not empty - then we need to catch up
		AHardwareBuffer * aHardwareBuffer;
		if (renderer->aHwBufferQueue.try_pop(aHardwareBuffer)) {
//      LOGI("Catching up as some more buffers could be consumed!");
			renderer->hwBufferToTexture(aHardwareBuffer);
		}
	}
}

void VulkanRenderer::hwBufferToVulkanTexture(AHardwareBuffer *aHardwareBuffer) {
	// TODO
	AHardwareBuffer_release(aHardwareBuffer);
}

void VulkanRenderer::renderImpl() {
	if (!ready) {
		LOGE("PREPARING!!");
	}
	uint32_t index;

	auto res = acquire_next_image(context_, &index);

	// TODO uncomment when implement resize and understand it better in general
//	// Handle outdated error in acquire.
//	if (res == vk::Result::eSuboptimalKHR || res == vk::Result::eErrorOutOfDateKHR)
//	{
//		resize(context_.swapchain_dimensions.width, context_.swapchain_dimensions.height);
//		res = acquire_next_image(context_, &index);
//	}

	if (res != vk::Result::eSuccess)
	{
		context_.queue.waitIdle();
		return;
	}

	render_triangle(context_, index);
	res = present_image(context_, index);

	// TODO uncomment when implement resize and understand it better in general
//	// Handle Outdated error in present.
//	if (res == vk::Result::eSuboptimalKHR || res == vk::Result::eErrorOutOfDateKHR) {
//		resize(context_.swapchain_dimensions.width, context_.swapchain_dimensions.height);
//	} else
	if (res != vk::Result::eSuccess) {
		LOGE("Failed to present swapchain image.");
	}
}

vk::Result VulkanRenderer::acquire_next_image(Context &context, uint32_t *image) {
	vk::Semaphore acquire_semaphore;
	if (context.recycled_semaphores.empty()) {
		acquire_semaphore = context.device.createSemaphore({});
	} else {
		acquire_semaphore = context.recycled_semaphores.back();
		context.recycled_semaphores.pop_back();
	}

	vk::Result res;
	std::tie(res, *image) = context.device.acquireNextImageKHR(context.swapchain, UINT64_MAX, acquire_semaphore);

	if (res != vk::Result::eSuccess) {
		context.recycled_semaphores.push_back(acquire_semaphore);
		return res;
	}

	// If we have outstanding fences for this swapchain image, wait for them to complete first.
	// After begin frame returns, it is safe to reuse or delete resources which
	// were used previously.
	//
	// We wait for fences which completes N frames earlier, so we do not stall,
	// waiting for all GPU work to complete before this returns.
	// Normally, this doesn't really block at all,
	// since we're waiting for old frames to have been completed, but just in case.
	if (context.per_frame[*image].queue_submit_fence) {
		context.device.waitForFences(context.per_frame[*image].queue_submit_fence, true, UINT64_MAX);
		context.device.resetFences(context.per_frame[*image].queue_submit_fence);
	}

	if (context.per_frame[*image].primary_command_pool) {
		context.device.resetCommandPool(context.per_frame[*image].primary_command_pool);
	}

	// Recycle the old semaphore back into the semaphore manager.
	vk::Semaphore old_semaphore = context.per_frame[*image].swapchain_acquire_semaphore;

	if (old_semaphore) {
		context.recycled_semaphores.push_back(old_semaphore);
	}

	context.per_frame[*image].swapchain_acquire_semaphore = acquire_semaphore;

	return vk::Result::eSuccess;
}

void VulkanRenderer::render_triangle(Context &context, uint32_t swapchain_index) {
// Render to this framebuffer.
	vk::Framebuffer framebuffer = context.swapchain_framebuffers[swapchain_index];

	// Allocate or re-use a primary command buffer.
	vk::CommandBuffer cmd = context.per_frame[swapchain_index].primary_command_buffer;

	// We will only submit this once before it's recycled.
	vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	// Begin command recording
	cmd.begin(begin_info);

	// Set clear color values.
	vk::ClearValue clear_value;
	clear_value.color = vk::ClearColorValue(std::array<float, 4>({{0.01f, 0.01f, 0.033f, 1.0f}}));

	// Begin the render pass.
	vk::RenderPassBeginInfo rp_begin(context.render_pass, framebuffer, {{0, 0}, {context.swapchain_dimensions.width, context.swapchain_dimensions.height}},
	                                 clear_value);
	// We will add draw commands in the same command buffer.
	cmd.beginRenderPass(rp_begin, vk::SubpassContents::eInline);

	// Bind the graphics pipeline.
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, context.pipeline);

	vk::Viewport vp(0.0f, 0.0f, static_cast<float>(context.swapchain_dimensions.width), static_cast<float>(context.swapchain_dimensions.height), 0.0f, 1.0f);
	// Set viewport dynamically
	cmd.setViewport(0, vp);

	vk::Rect2D scissor({0, 0}, {context.swapchain_dimensions.width, context.swapchain_dimensions.height});
	// Set scissor dynamically
	cmd.setScissor(0, scissor);

	// Draw three vertices with one instance.
	cmd.draw(3, 1, 0, 0);

	// Complete render pass.
	cmd.endRenderPass();

	// Complete the command buffer.
	cmd.end();

	// Submit it to the queue with a release semaphore.
	if (!context.per_frame[swapchain_index].swapchain_release_semaphore)
	{
		context.per_frame[swapchain_index].swapchain_release_semaphore = context.device.createSemaphore({});
	}

	vk::PipelineStageFlags wait_stage{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

	vk::SubmitInfo info(context.per_frame[swapchain_index].swapchain_acquire_semaphore, wait_stage, cmd,
	                    context.per_frame[swapchain_index].swapchain_release_semaphore);
	// Submit command buffer to graphics queue
	context.queue.submit(info, context.per_frame[swapchain_index].queue_submit_fence);
}

vk::Result VulkanRenderer::present_image(Context &context, uint32_t index) {
	vk::PresentInfoKHR present(context.per_frame[index].swapchain_release_semaphore, context.swapchain, index);
	// Present swapchain image
	return context.queue.presentKHR(present);
}

} // namespace android
} // namespace engine