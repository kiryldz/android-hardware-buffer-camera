#pragma once

// could link only as dynamic on Android
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VK_USE_PLATFORM_ANDROID_KHR 1
#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_android.h>

#include <map>

#include <ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include "base_renderer.hpp"

namespace engine {
namespace android {

/**
* @brief Swapchain state
*/
struct SwapchainDimensions {
	/// Width of the swapchain.
	uint32_t width = 0;

	/// Height of the swapchain.
	uint32_t height = 0;

	/// Pixel format of the swapchain.
	vk::Format format = vk::Format::eUndefined;
};

/**
* @brief Per-frame data
*/
struct PerFrame {
	vk::Device device;

	vk::Fence queue_submit_fence;

	vk::CommandPool primary_command_pool;

	vk::CommandBuffer primary_command_buffer;

	vk::Semaphore swapchain_acquire_semaphore;

	vk::Semaphore swapchain_release_semaphore;

	int32_t queue_index;
};

/**
* @brief Vulkan objects and global state
*/
struct Context {
	/// The Vulkan instance.
	vk::Instance instance;

	/// The Vulkan physical device.
	vk::PhysicalDevice gpu;

	/// The Vulkan device.
	vk::Device device;

	/// The Vulkan device queue.
	vk::Queue queue;

	/// The swapchain.
	vk::SwapchainKHR swapchain;

	/// The swapchain dimensions.
	SwapchainDimensions swapchain_dimensions;

	/// The surface we will render to.
	vk::SurfaceKHR surface;

	/// The queue family index where graphics work will be submitted.
	int32_t graphics_queue_index = -1;

	/// The image view for each swapchain image.
	std::vector<vk::ImageView> swapchain_image_views;

	/// The framebuffer for each swapchain image view.
	std::vector<vk::Framebuffer> swapchain_framebuffers;

	/// The renderpass description.
	vk::RenderPass render_pass;

	/// The graphics pipeline.
	vk::Pipeline pipeline;

	/**
	 * The pipeline layout for resources.
	 * Not used in this sample, but we still need to provide a dummy one.
	 */
	vk::PipelineLayout pipeline_layout;

	/// The debug report callback.
	vk::DebugReportCallbackEXT debug_callback;

	/// A set of semaphores that can be reused.
	std::vector<vk::Semaphore> recycled_semaphores;

	/// A set of per-frame data.
	std::vector<PerFrame> per_frame;
};

class VulkanRenderer : public BaseRenderer {

protected:
	const char *renderingModeName() override {
		return (const char *) "Vulkan";
	}

	bool onWindowCreated() override {
		init_instance(context_, {VK_KHR_SURFACE_EXTENSION_NAME}, {});
		select_physical_device_and_surface(context_);
		init_device(
			context_,
			{
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME
			});
		return true;
	}

	void onWindowSizeUpdated(int width, int height) override {
		context_.swapchain_dimensions.width  = width;
		context_.swapchain_dimensions.height = height;

		init_swapchain(context_);

		// Create the necessary objects for rendering.
		init_render_pass(context_);
		init_pipeline(context_);
		init_framebuffers(context_);

		ready = true;
	}

	void onWindowDestroyed() override {
		// TODO
	}

	void hwBufferToTexture(AHardwareBuffer *buffer) override {
		hwBufferToVulkanTexture(buffer);
	}

	bool couldRender() const override {
		return ready;
	}

	void render() override {
		// TODO think of more elegant solution, smth with friend I assume
		renderImpl();
	}

	void postChoreographerCallback() override {
		// posting next frame callback, no need to explicitly wake the looper afterwards
		// as AChoreographer seems to operate with it's own fd and callbacks
		AChoreographer_postFrameCallback(aChoreographer, doFrame, this);
	}

private:
	Context context_;
	bool ready = false;

	const char* vertexShader = "#version 320 es\n"
	                           "precision mediump float;\n"
	                           "\n"
	                           "layout(location = 0) out vec3 out_color;\n"
	                           "\n"
	                           "vec2 triangle_positions[3] = vec2[](\n"
	                           "    vec2(0.5, -0.5),\n"
	                           "    vec2(0.5, 0.5),\n"
	                           "    vec2(-0.5, 0.5)\n"
	                           ");\n"
	                           "\n"
	                           "vec3 triangle_colors[3] = vec3[](\n"
	                           "    vec3(1.0, 0.0, 0.0),\n"
	                           "    vec3(0.0, 1.0, 0.0),\n"
	                           "    vec3(0.0, 0.0, 1.0)\n"
	                           ");\n"
	                           "\n"
	                           "void main()\n"
	                           "{\n"
	                           "    gl_Position = vec4(triangle_positions[gl_VertexIndex], 0.0, 1.0);\n"
	                           "\n"
	                           "    out_color = triangle_colors[gl_VertexIndex];\n"
	                           "}";

	const char* fragmentShader = "#version 320 es\n"
	                           "\n"
	                           "precision mediump float;\n"
	                           "\n"
	                           "layout(location = 0) in vec3 in_color;\n"
	                           "layout(location = 0) out vec4 out_color;\n"
	                           "void main()\n"
	                           "{\n"
	                           "\tout_color = vec4(in_color, 1.0);\n"
	                           "}";

	void init_instance(Context &                        context,
	                   const std::vector<const char *> &required_instance_extensions,
	                   const std::vector<const char *> &required_validation_layers);

	void select_physical_device_and_surface(Context &context);

	void init_device(Context &                        context,
	                 const std::vector<const char *> &required_device_extensions);

	void init_per_frame(Context &context, PerFrame &per_frame);

	void init_swapchain(Context &context);

	void init_render_pass(Context &context);

	vk::ShaderModule load_shader_module(Context &context,
																			const char *shaderName,
																			EShLanguage shaderKind);

	void init_pipeline(Context &context);

	void init_framebuffers(Context &context);

	/**
   * Always called from render thread.
   * @param aHardwareBuffer
   */
	void hwBufferToVulkanTexture(AHardwareBuffer * aHardwareBuffer);

	///////// Callbacks for AChoreographer and ALooper stored as private static functions

	static void doFrame(long timeStampNanos, void* data);

	void renderImpl();

	vk::Result acquire_next_image(Context &context, uint32_t *image);

	void render_triangle(Context &context, uint32_t swapchain_index);

	vk::Result present_image(Context &context, uint32_t index);
};

} // namespace android
} // namespace engine
