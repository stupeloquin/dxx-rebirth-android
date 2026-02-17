/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * Vulkan renderer - shared state and declarations.
 */

#pragma once

#if DXX_USE_VULKAN

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <array>
#include <vector>
#include <cstring>
#include <cmath>

#include "pstypes.h"
#include "gr.h"
#include "3d.h"

namespace dcx {

// Maximum frames in flight for double-buffering
constexpr uint32_t VK_MAX_FRAMES_IN_FLIGHT = 2;

// Per-frame ring buffer size for vertex data (4MB per frame)
constexpr VkDeviceSize VK_VERTEX_RING_SIZE = 4 * 1024 * 1024;

// Push constant layout: MVP matrix (64 bytes) + flags (16 bytes)
struct vk_push_constants {
	float mvp[16];       // 4x4 column-major matrix
	float alpha_ref;     // alpha test reference value
	float pad[3];
};
static_assert(sizeof(vk_push_constants) == 80);

// Vertex format for all draw calls
struct vk_vertex {
	float x, y, z;      // position
	float r, g, b, a;   // color
	float u, v;          // texcoord
};

// Pipeline variant indices
enum vk_pipeline_id {
	VK_PIPE_TEXTURED_3D = 0,  // textured 3D geometry (walls, robots)
	VK_PIPE_FLAT_3D,           // flat-shaded 3D (lasers, drone arms)
	VK_PIPE_LINE_3D,           // 3D lines
	VK_PIPE_TEXTURED_2D,       // 2D textured (bitmaps, UI)
	VK_PIPE_FLAT_2D,           // 2D flat color (rectangles)
	VK_PIPE_LINE_2D,           // 2D lines
	VK_PIPE_COUNT
};

// Blend mode
enum vk_blend_mode {
	VK_BLEND_NORMAL = 0,      // src_alpha, 1-src_alpha
	VK_BLEND_ADDITIVE_A,      // src_alpha, one
	VK_BLEND_ADDITIVE_C,      // one, one
	VK_BLEND_COUNT
};

// Vulkan texture handle
struct vk_texture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	uint32_t w = 0, h = 0;
	uint32_t tw = 0, th = 0;  // padded to power-of-two
	float u_scale = 1.0f, v_scale = 1.0f;
	bool valid = false;
};

// Per-frame resources
struct vk_frame_data {
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkSemaphore image_available = VK_NULL_HANDLE;
	VkSemaphore render_finished = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	// Vertex ring buffer
	VkBuffer vertex_buffer = VK_NULL_HANDLE;
	VmaAllocation vertex_allocation = VK_NULL_HANDLE;
	void *vertex_mapped = nullptr;
	VkDeviceSize vertex_offset = 0;
};

// Global Vulkan state
struct vk_state {
	// Instance and device
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue graphics_queue = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	VmaAllocator allocator = VK_NULL_HANDLE;

	// Surface and swapchain
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
	VkExtent2D swapchain_extent = {0, 0};
	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_views;

	// Render pass and framebuffers
	VkRenderPass render_pass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> framebuffers;

	// Depth buffer
	VkImage depth_image = VK_NULL_HANDLE;
	VmaAllocation depth_allocation = VK_NULL_HANDLE;
	VkImageView depth_view = VK_NULL_HANDLE;

	// Command pool
	VkCommandPool command_pool = VK_NULL_HANDLE;

	// Descriptor pool and layout for textures
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

	// Pipeline layout (shared by all pipelines)
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

	// Pipelines for each variant and blend mode
	VkPipeline pipelines[VK_PIPE_COUNT][VK_BLEND_COUNT] = {};

	// Per-frame data
	std::array<vk_frame_data, VK_MAX_FRAMES_IN_FLIGHT> frames;
	uint32_t current_frame = 0;
	uint32_t current_image_index = 0;

	// White 1x1 texture used when no texture is bound
	vk_texture white_texture;

	// Currently bound texture descriptor set
	VkDescriptorSet bound_texture = VK_NULL_HANDLE;

	// Current state
	bool in_render_pass = false;
	bool frame_started = false;
	bool initialized = false;

	// Screen dimensions
	uint32_t screen_width = 0;
	uint32_t screen_height = 0;

	// Current blend mode
	vk_blend_mode current_blend = VK_BLEND_NORMAL;

	// Current pipeline mode (3D vs 2D)
	bool is_3d_mode = false;

	// Software matrix stack
	float projection_matrix[16];
	float modelview_matrix[16];
	float mvp_matrix[16];
	static constexpr int MAX_MATRIX_STACK = 8;
	float projection_stack[MAX_MATRIX_STACK][16];
	float modelview_stack[MAX_MATRIX_STACK][16];
	int projection_stack_depth = 0;
	int modelview_stack_depth = 0;
};

extern vk_state g_vk;

// Matrix utilities
void vk_mat4_identity(float m[16]);
void vk_mat4_ortho(float m[16], float l, float r, float b, float t, float n, float f);
void vk_mat4_perspective(float m[16], float fovy_deg, float aspect, float near, float far);
void vk_mat4_multiply(float out[16], const float a[16], const float b[16]);
void vk_update_mvp();

// Init/shutdown
bool vk_init(struct SDL_Window *window, uint32_t w, uint32_t h);
void vk_shutdown();
bool vk_recreate_swapchain(uint32_t w, uint32_t h);

// Pipeline creation
bool vk_create_pipelines();
void vk_destroy_pipelines();

// Texture management
vk_texture *vk_create_texture(uint32_t w, uint32_t h, const uint8_t *rgba_data);
void vk_destroy_texture(vk_texture *tex);
void vk_bind_texture(vk_texture *tex);

// Frame lifecycle
bool vk_begin_frame();
void vk_end_frame();
void vk_present();

// Drawing - these match the existing OGL function signatures
void vk_draw_triangles(const vk_vertex *verts, uint32_t count, bool textured, bool is_3d);
void vk_draw_triangle_fan(const vk_vertex *verts, uint32_t count, bool textured, bool is_3d);
void vk_draw_lines(const vk_vertex *verts, uint32_t count, bool is_3d);

}  // namespace dcx

#endif  // DXX_USE_VULKAN
