/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * Vulkan renderer - initialization, swapchain, render pass.
 */

#include "dxxsconf.h"
#if DXX_USE_VULKAN

#include "vk_common.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include "console.h"
#include "dxxerror.h"

// Use only Vulkan 1.0 functions (Android can't statically link 1.1+)
#define VMA_VULKAN_VERSION 1000000
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

namespace dcx {

vk_state g_vk;

void vk_mat4_identity(float m[16])
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void vk_mat4_ortho(float m[16], float l, float r, float b, float t, float n, float f)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = 2.0f / (r - l);
	m[5] = 2.0f / (t - b);
	m[10] = -2.0f / (f - n);
	m[12] = -(r + l) / (r - l);
	m[13] = -(t + b) / (t - b);
	m[14] = -(f + n) / (f - n);
	m[15] = 1.0f;
}

void vk_mat4_perspective(float m[16], float fovy_deg, float aspect, float near_val, float far_val)
{
	memset(m, 0, 16 * sizeof(float));
	const float fovy_rad = fovy_deg * static_cast<float>(M_PI) / 180.0f;
	const float f = 1.0f / tanf(fovy_rad / 2.0f);
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (far_val + near_val) / (near_val - far_val);
	m[11] = -1.0f;
	m[14] = (2.0f * far_val * near_val) / (near_val - far_val);
}

void vk_mat4_multiply(float out[16], const float a[16], const float b[16])
{
	float tmp[16];
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
		{
			tmp[j * 4 + i] = 0;
			for (int k = 0; k < 4; k++)
				tmp[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
		}
	memcpy(out, tmp, sizeof(tmp));
}

void vk_update_mvp()
{
	vk_mat4_multiply(g_vk.mvp_matrix, g_vk.projection_matrix, g_vk.modelview_matrix);
}

static bool vk_create_instance(SDL_Window *window)
{
	VkApplicationInfo app_info{};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "D1X-Rebirth";
	app_info.applicationVersion = VK_MAKE_VERSION(0, 61, 0);
	app_info.pEngineName = "DXX-Rebirth";
	app_info.engineVersion = VK_MAKE_VERSION(0, 61, 0);
	app_info.apiVersion = VK_API_VERSION_1_1;

	unsigned int ext_count = 0;
	SDL_Vulkan_GetInstanceExtensions(window, &ext_count, nullptr);
	std::vector<const char *> extensions(ext_count);
	SDL_Vulkan_GetInstanceExtensions(window, &ext_count, extensions.data());

	VkInstanceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ci.pApplicationInfo = &app_info;
	ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	ci.ppEnabledExtensionNames = extensions.data();

#ifndef NDEBUG
	static const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
	ci.enabledLayerCount = 1;
	ci.ppEnabledLayerNames = validation_layers;
#endif

	if (vkCreateInstance(&ci, nullptr, &g_vk.instance) != VK_SUCCESS)
	{
#ifndef NDEBUG
		// Retry without validation layers
		ci.enabledLayerCount = 0;
		ci.ppEnabledLayerNames = nullptr;
		if (vkCreateInstance(&ci, nullptr, &g_vk.instance) != VK_SUCCESS)
#endif
		{
			con_puts(CON_URGENT, "VK: Failed to create Vulkan instance");
			return false;
		}
	}
	con_puts(CON_DEBUG, "VK: Instance created");
	return true;
}

static bool vk_select_physical_device()
{
	uint32_t count = 0;
	vkEnumeratePhysicalDevices(g_vk.instance, &count, nullptr);
	if (count == 0)
	{
		con_puts(CON_URGENT, "VK: No Vulkan-capable GPU found");
		return false;
	}
	std::vector<VkPhysicalDevice> devices(count);
	vkEnumeratePhysicalDevices(g_vk.instance, &count, devices.data());

	// Pick first device (on Android there's typically only one)
	g_vk.physical_device = devices[0];

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(g_vk.physical_device, &props);
	con_printf(CON_DEBUG, "VK: Using GPU: %s", props.deviceName);
	return true;
}

static bool vk_create_device()
{
	// Find graphics queue family
	uint32_t qf_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(g_vk.physical_device, &qf_count, nullptr);
	std::vector<VkQueueFamilyProperties> qf_props(qf_count);
	vkGetPhysicalDeviceQueueFamilyProperties(g_vk.physical_device, &qf_count, qf_props.data());

	g_vk.queue_family = UINT32_MAX;
	for (uint32_t i = 0; i < qf_count; i++)
	{
		if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			VkBool32 present_support = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(g_vk.physical_device, i, g_vk.surface, &present_support);
			if (present_support)
			{
				g_vk.queue_family = i;
				break;
			}
		}
	}
	if (g_vk.queue_family == UINT32_MAX)
	{
		con_puts(CON_URGENT, "VK: No suitable queue family");
		return false;
	}

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queue_ci{};
	queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_ci.queueFamilyIndex = g_vk.queue_family;
	queue_ci.queueCount = 1;
	queue_ci.pQueuePriorities = &priority;

	static const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	VkDeviceCreateInfo dev_ci{};
	dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dev_ci.queueCreateInfoCount = 1;
	dev_ci.pQueueCreateInfos = &queue_ci;
	dev_ci.enabledExtensionCount = 1;
	dev_ci.ppEnabledExtensionNames = device_extensions;

	if (vkCreateDevice(g_vk.physical_device, &dev_ci, nullptr, &g_vk.device) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create logical device");
		return false;
	}
	vkGetDeviceQueue(g_vk.device, g_vk.queue_family, 0, &g_vk.graphics_queue);
	con_puts(CON_DEBUG, "VK: Device created");
	return true;
}

static bool vk_create_allocator()
{
	VmaAllocatorCreateInfo ci{};
	ci.physicalDevice = g_vk.physical_device;
	ci.device = g_vk.device;
	ci.instance = g_vk.instance;
	ci.vulkanApiVersion = VK_API_VERSION_1_1;

	if (vmaCreateAllocator(&ci, &g_vk.allocator) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create VMA allocator");
		return false;
	}
	return true;
}

static bool vk_create_swapchain(uint32_t w, uint32_t h)
{
	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.physical_device, g_vk.surface, &caps);

	uint32_t fmt_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical_device, g_vk.surface, &fmt_count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(fmt_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical_device, g_vk.surface, &fmt_count, formats.data());

	// Prefer SRGB, fall back to first available
	VkSurfaceFormatKHR chosen = formats[0];
	for (auto &f : formats)
	{
		if (f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB)
		{
			chosen = f;
			break;
		}
	}
	g_vk.swapchain_format = chosen.format;

	g_vk.swapchain_extent.width = w;
	g_vk.swapchain_extent.height = h;
	if (caps.currentExtent.width != UINT32_MAX)
		g_vk.swapchain_extent = caps.currentExtent;

	uint32_t image_count = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
		image_count = caps.maxImageCount;

	VkSwapchainCreateInfoKHR sci{};
	sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	sci.surface = g_vk.surface;
	sci.minImageCount = image_count;
	sci.imageFormat = chosen.format;
	sci.imageColorSpace = chosen.colorSpace;
	sci.imageExtent = g_vk.swapchain_extent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sci.preTransform = caps.currentTransform;
	sci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // guaranteed available, vsync
	sci.clipped = VK_TRUE;
	sci.oldSwapchain = g_vk.swapchain;

	if (vkCreateSwapchainKHR(g_vk.device, &sci, nullptr, &g_vk.swapchain) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create swapchain");
		return false;
	}

	// Destroy old swapchain if any
	if (sci.oldSwapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(g_vk.device, sci.oldSwapchain, nullptr);

	vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &image_count, nullptr);
	g_vk.swapchain_images.resize(image_count);
	vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &image_count, g_vk.swapchain_images.data());

	// Create image views
	g_vk.swapchain_views.resize(image_count);
	for (uint32_t i = 0; i < image_count; i++)
	{
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = g_vk.swapchain_images[i];
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = g_vk.swapchain_format;
		vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.levelCount = 1;
		vci.subresourceRange.layerCount = 1;
		if (vkCreateImageView(g_vk.device, &vci, nullptr, &g_vk.swapchain_views[i]) != VK_SUCCESS)
		{
			con_puts(CON_URGENT, "VK: Failed to create swapchain image view");
			return false;
		}
	}

	con_printf(CON_DEBUG, "VK: Swapchain created %ux%u, %u images", 
		g_vk.swapchain_extent.width, g_vk.swapchain_extent.height, image_count);
	return true;
}

static bool vk_create_depth_buffer()
{
	VkImageCreateInfo ici{};
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = VK_FORMAT_D32_SFLOAT;
	ici.extent = {g_vk.swapchain_extent.width, g_vk.swapchain_extent.height, 1};
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VmaAllocationCreateInfo ai{};
	ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	if (vmaCreateImage(g_vk.allocator, &ici, &ai, &g_vk.depth_image, &g_vk.depth_allocation, nullptr) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create depth buffer");
		return false;
	}

	VkImageViewCreateInfo vci{};
	vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vci.image = g_vk.depth_image;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = VK_FORMAT_D32_SFLOAT;
	vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	vci.subresourceRange.levelCount = 1;
	vci.subresourceRange.layerCount = 1;

	if (vkCreateImageView(g_vk.device, &vci, nullptr, &g_vk.depth_view) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create depth image view");
		return false;
	}
	return true;
}

static bool vk_create_render_pass()
{
	std::array<VkAttachmentDescription, 2> attachments{};
	// Color
	attachments[0].format = g_vk.swapchain_format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	// Depth
	attachments[1].format = VK_FORMAT_D32_SFLOAT;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;
	subpass.pDepthStencilAttachment = &depth_ref;

	VkSubpassDependency dep{};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo rpci{};
	rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
	rpci.pAttachments = attachments.data();
	rpci.subpassCount = 1;
	rpci.pSubpasses = &subpass;
	rpci.dependencyCount = 1;
	rpci.pDependencies = &dep;

	if (vkCreateRenderPass(g_vk.device, &rpci, nullptr, &g_vk.render_pass) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create render pass");
		return false;
	}
	return true;
}

static bool vk_create_framebuffers()
{
	g_vk.framebuffers.resize(g_vk.swapchain_views.size());
	for (size_t i = 0; i < g_vk.swapchain_views.size(); i++)
	{
		std::array<VkImageView, 2> att = {g_vk.swapchain_views[i], g_vk.depth_view};
		VkFramebufferCreateInfo fbci{};
		fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbci.renderPass = g_vk.render_pass;
		fbci.attachmentCount = static_cast<uint32_t>(att.size());
		fbci.pAttachments = att.data();
		fbci.width = g_vk.swapchain_extent.width;
		fbci.height = g_vk.swapchain_extent.height;
		fbci.layers = 1;

		if (vkCreateFramebuffer(g_vk.device, &fbci, nullptr, &g_vk.framebuffers[i]) != VK_SUCCESS)
		{
			con_puts(CON_URGENT, "VK: Failed to create framebuffer");
			return false;
		}
	}
	return true;
}

static bool vk_create_command_pool()
{
	VkCommandPoolCreateInfo cpci{};
	cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpci.queueFamilyIndex = g_vk.queue_family;

	if (vkCreateCommandPool(g_vk.device, &cpci, nullptr, &g_vk.command_pool) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create command pool");
		return false;
	}
	return true;
}

static bool vk_create_descriptor_pool()
{
	// Pool for texture samplers - support up to 1024 textures
	VkDescriptorPoolSize pool_size{};
	pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_size.descriptorCount = 1024;

	VkDescriptorPoolCreateInfo dpci{};
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	dpci.maxSets = 1024;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes = &pool_size;

	if (vkCreateDescriptorPool(g_vk.device, &dpci, nullptr, &g_vk.descriptor_pool) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create descriptor pool");
		return false;
	}

	// Descriptor set layout: binding 0 = combined image sampler
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dslci{};
	dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 1;
	dslci.pBindings = &binding;

	if (vkCreateDescriptorSetLayout(g_vk.device, &dslci, nullptr, &g_vk.descriptor_set_layout) != VK_SUCCESS)
	{
		con_puts(CON_URGENT, "VK: Failed to create descriptor set layout");
		return false;
	}
	return true;
}

static bool vk_create_per_frame_resources()
{
	VkCommandBufferAllocateInfo cbai{};
	cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandPool = g_vk.command_pool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;

	for (auto &frame : g_vk.frames)
	{
		if (vkAllocateCommandBuffers(g_vk.device, &cbai, &frame.cmd) != VK_SUCCESS)
			return false;

		VkSemaphoreCreateInfo sci{};
		sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCreateSemaphore(g_vk.device, &sci, nullptr, &frame.image_available);
		vkCreateSemaphore(g_vk.device, &sci, nullptr, &frame.render_finished);

		VkFenceCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		vkCreateFence(g_vk.device, &fci, nullptr, &frame.fence);

		// Vertex ring buffer (host-visible, coherent)
		VkBufferCreateInfo bci{};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = VK_VERTEX_RING_SIZE;
		bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		VmaAllocationCreateInfo ai{};
		ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VmaAllocationInfo alloc_info{};
		if (vmaCreateBuffer(g_vk.allocator, &bci, &ai, &frame.vertex_buffer,
		                    &frame.vertex_allocation, &alloc_info) != VK_SUCCESS)
		{
			con_puts(CON_URGENT, "VK: Failed to create vertex ring buffer");
			return false;
		}
		frame.vertex_mapped = alloc_info.pMappedData;
		frame.vertex_offset = 0;
	}
	return true;
}

static bool vk_create_white_texture()
{
	const uint8_t white[] = {255, 255, 255, 255};
	auto *tex = vk_create_texture(1, 1, white);
	if (!tex)
		return false;
	g_vk.white_texture = *tex;
	return true;
}

bool vk_init(SDL_Window *window, uint32_t w, uint32_t h)
{
	g_vk.screen_width = w;
	g_vk.screen_height = h;

	if (!vk_create_instance(window))
		return false;

	if (!SDL_Vulkan_CreateSurface(window, g_vk.instance, &g_vk.surface))
	{
		con_printf(CON_URGENT, "VK: SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
		return false;
	}

	if (!vk_select_physical_device())
		return false;
	if (!vk_create_device())
		return false;
	if (!vk_create_allocator())
		return false;
	if (!vk_create_swapchain(w, h))
		return false;
	if (!vk_create_depth_buffer())
		return false;
	if (!vk_create_render_pass())
		return false;
	if (!vk_create_framebuffers())
		return false;
	if (!vk_create_command_pool())
		return false;
	if (!vk_create_descriptor_pool())
		return false;
	if (!vk_create_per_frame_resources())
		return false;
	if (!vk_create_pipelines())
		return false;
	if (!vk_create_white_texture())
		return false;

	vk_mat4_identity(g_vk.projection_matrix);
	vk_mat4_identity(g_vk.modelview_matrix);
	vk_mat4_identity(g_vk.mvp_matrix);

	g_vk.initialized = true;
	con_puts(CON_DEBUG, "VK: Initialization complete");
	return true;
}

bool vk_recreate_swapchain(uint32_t w, uint32_t h)
{
	vkDeviceWaitIdle(g_vk.device);

	// Cleanup old framebuffers and views
	for (auto fb : g_vk.framebuffers)
		vkDestroyFramebuffer(g_vk.device, fb, nullptr);
	g_vk.framebuffers.clear();

	for (auto iv : g_vk.swapchain_views)
		vkDestroyImageView(g_vk.device, iv, nullptr);
	g_vk.swapchain_views.clear();

	if (g_vk.depth_view)
		vkDestroyImageView(g_vk.device, g_vk.depth_view, nullptr);
	if (g_vk.depth_image)
		vmaDestroyImage(g_vk.allocator, g_vk.depth_image, g_vk.depth_allocation);

	if (!vk_create_swapchain(w, h))
		return false;
	if (!vk_create_depth_buffer())
		return false;
	if (!vk_create_framebuffers())
		return false;

	g_vk.screen_width = w;
	g_vk.screen_height = h;
	return true;
}

void vk_shutdown()
{
	if (!g_vk.initialized)
		return;

	vkDeviceWaitIdle(g_vk.device);

	// Destroy white texture
	vk_destroy_texture(&g_vk.white_texture);

	// Destroy per-frame resources
	for (auto &frame : g_vk.frames)
	{
		if (frame.vertex_buffer)
			vmaDestroyBuffer(g_vk.allocator, frame.vertex_buffer, frame.vertex_allocation);
		if (frame.fence)
			vkDestroyFence(g_vk.device, frame.fence, nullptr);
		if (frame.render_finished)
			vkDestroySemaphore(g_vk.device, frame.render_finished, nullptr);
		if (frame.image_available)
			vkDestroySemaphore(g_vk.device, frame.image_available, nullptr);
	}

	vk_destroy_pipelines();

	if (g_vk.descriptor_set_layout)
		vkDestroyDescriptorSetLayout(g_vk.device, g_vk.descriptor_set_layout, nullptr);
	if (g_vk.descriptor_pool)
		vkDestroyDescriptorPool(g_vk.device, g_vk.descriptor_pool, nullptr);
	if (g_vk.command_pool)
		vkDestroyCommandPool(g_vk.device, g_vk.command_pool, nullptr);

	for (auto fb : g_vk.framebuffers)
		vkDestroyFramebuffer(g_vk.device, fb, nullptr);
	if (g_vk.render_pass)
		vkDestroyRenderPass(g_vk.device, g_vk.render_pass, nullptr);

	if (g_vk.depth_view)
		vkDestroyImageView(g_vk.device, g_vk.depth_view, nullptr);
	if (g_vk.depth_image)
		vmaDestroyImage(g_vk.allocator, g_vk.depth_image, g_vk.depth_allocation);

	for (auto iv : g_vk.swapchain_views)
		vkDestroyImageView(g_vk.device, iv, nullptr);
	if (g_vk.swapchain)
		vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, nullptr);

	if (g_vk.allocator)
		vmaDestroyAllocator(g_vk.allocator);
	if (g_vk.device)
		vkDestroyDevice(g_vk.device, nullptr);
	if (g_vk.surface)
		vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, nullptr);
	if (g_vk.instance)
		vkDestroyInstance(g_vk.instance, nullptr);

	g_vk = {};
	con_puts(CON_DEBUG, "VK: Shutdown complete");
}

bool vk_begin_frame()
{
	if (!g_vk.initialized)
		return false;

	auto &frame = g_vk.frames[g_vk.current_frame];

	vkWaitForFences(g_vk.device, 1, &frame.fence, VK_TRUE, UINT64_MAX);

	VkResult result = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX,
	                                         frame.image_available, VK_NULL_HANDLE,
	                                         &g_vk.current_image_index);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		vk_recreate_swapchain(g_vk.screen_width, g_vk.screen_height);
		return false;
	}

	vkResetFences(g_vk.device, 1, &frame.fence);
	vkResetCommandBuffer(frame.cmd, 0);

	VkCommandBufferBeginInfo begin_info{};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(frame.cmd, &begin_info);

	std::array<VkClearValue, 2> clear_values{};
	clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
	clear_values[1].depthStencil = {1.0f, 0};

	VkRenderPassBeginInfo rpbi{};
	rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpbi.renderPass = g_vk.render_pass;
	rpbi.framebuffer = g_vk.framebuffers[g_vk.current_image_index];
	rpbi.renderArea.extent = g_vk.swapchain_extent;
	rpbi.clearValueCount = static_cast<uint32_t>(clear_values.size());
	rpbi.pClearValues = clear_values.data();
	vkCmdBeginRenderPass(frame.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

	g_vk.in_render_pass = true;
	g_vk.frame_started = true;
	frame.vertex_offset = 0;
	g_vk.bound_texture = VK_NULL_HANDLE;

	return true;
}

void vk_end_frame()
{
	if (!g_vk.frame_started)
		return;

	auto &frame = g_vk.frames[g_vk.current_frame];

	if (g_vk.in_render_pass)
	{
		vkCmdEndRenderPass(frame.cmd);
		g_vk.in_render_pass = false;
	}

	vkEndCommandBuffer(frame.cmd);
	g_vk.frame_started = false;
}

void vk_present()
{
	auto &frame = g_vk.frames[g_vk.current_frame];

	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &frame.image_available;
	submit.pWaitDstStageMask = &wait_stage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &frame.cmd;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &frame.render_finished;

	vkQueueSubmit(g_vk.graphics_queue, 1, &submit, frame.fence);

	VkPresentInfoKHR present{};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &frame.render_finished;
	present.swapchainCount = 1;
	present.pSwapchains = &g_vk.swapchain;
	present.pImageIndices = &g_vk.current_image_index;

	vkQueuePresentKHR(g_vk.graphics_queue, &present);

	g_vk.current_frame = (g_vk.current_frame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
}

}  // namespace dcx

#endif  // DXX_USE_VULKAN
