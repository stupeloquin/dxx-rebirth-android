/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * Vulkan renderer - texture upload and management.
 */

#include "dxxsconf.h"
#if DXX_USE_VULKAN

#include "vk_common.h"
#include "console.h"

namespace dcx {

static VkCommandBuffer vk_begin_single_command()
{
	VkCommandBufferAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ai.commandPool = g_vk.command_pool;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(g_vk.device, &ai, &cmd);

	VkCommandBufferBeginInfo bi{};
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &bi);
	return cmd;
}

static void vk_end_single_command(VkCommandBuffer cmd)
{
	vkEndCommandBuffer(cmd);

	VkSubmitInfo si{};
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;

	vkQueueSubmit(g_vk.graphics_queue, 1, &si, VK_NULL_HANDLE);
	vkQueueWaitIdle(g_vk.graphics_queue);

	vkFreeCommandBuffers(g_vk.device, g_vk.command_pool, 1, &cmd);
}

static uint32_t next_power_of_two(uint32_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

vk_texture *vk_create_texture(uint32_t w, uint32_t h, const uint8_t *rgba_data)
{
	auto *tex = new vk_texture();
	tex->w = w;
	tex->h = h;
	tex->tw = next_power_of_two(w);
	tex->th = next_power_of_two(h);
	tex->u_scale = static_cast<float>(w) / static_cast<float>(tex->tw);
	tex->v_scale = static_cast<float>(h) / static_cast<float>(tex->th);

	const VkDeviceSize image_size = static_cast<VkDeviceSize>(tex->tw) * tex->th * 4;

	// Create staging buffer
	VkBuffer staging_buffer;
	VmaAllocation staging_alloc;

	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = image_size;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo sai{};
	sai.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	if (vmaCreateBuffer(g_vk.allocator, &bci, &sai, &staging_buffer, &staging_alloc, nullptr) != VK_SUCCESS)
	{
		delete tex;
		return nullptr;
	}

	// Copy data to staging (pad if necessary)
	void *mapped;
	vmaMapMemory(g_vk.allocator, staging_alloc, &mapped);
	if (w == tex->tw && h == tex->th)
	{
		memcpy(mapped, rgba_data, image_size);
	}
	else
	{
		// Zero-fill padded area, then copy row by row
		memset(mapped, 0, image_size);
		auto *dst = static_cast<uint8_t *>(mapped);
		for (uint32_t row = 0; row < h; row++)
			memcpy(dst + row * tex->tw * 4, rgba_data + row * w * 4, w * 4);
	}
	vmaUnmapMemory(g_vk.allocator, staging_alloc);

	// Create image
	VkImageCreateInfo ici{};
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = VK_FORMAT_R8G8B8A8_UNORM;
	ici.extent = {tex->tw, tex->th, 1};
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	VmaAllocationCreateInfo iai{};
	iai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	if (vmaCreateImage(g_vk.allocator, &ici, &iai, &tex->image, &tex->allocation, nullptr) != VK_SUCCESS)
	{
		vmaDestroyBuffer(g_vk.allocator, staging_buffer, staging_alloc);
		delete tex;
		return nullptr;
	}

	// Transition image to transfer dst, copy, then transition to shader read
	auto cmd = vk_begin_single_command();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = tex->image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = {tex->tw, tex->th, 1};

	vkCmdCopyBufferToImage(cmd, staging_buffer, tex->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	vk_end_single_command(cmd);

	// Cleanup staging
	vmaDestroyBuffer(g_vk.allocator, staging_buffer, staging_alloc);

	// Create image view
	VkImageViewCreateInfo vci{};
	vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vci.image = tex->image;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = VK_FORMAT_R8G8B8A8_UNORM;
	vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vci.subresourceRange.levelCount = 1;
	vci.subresourceRange.layerCount = 1;

	if (vkCreateImageView(g_vk.device, &vci, nullptr, &tex->view) != VK_SUCCESS)
	{
		vk_destroy_texture(tex);
		return nullptr;
	}

	// Create sampler
	VkSamplerCreateInfo sci{};
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sci.magFilter = VK_FILTER_NEAREST;
	sci.minFilter = VK_FILTER_NEAREST;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.maxLod = 0.0f;

	if (vkCreateSampler(g_vk.device, &sci, nullptr, &tex->sampler) != VK_SUCCESS)
	{
		vk_destroy_texture(tex);
		return nullptr;
	}

	// Allocate and update descriptor set
	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool = g_vk.descriptor_pool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &g_vk.descriptor_set_layout;

	if (vkAllocateDescriptorSets(g_vk.device, &dsai, &tex->descriptor_set) != VK_SUCCESS)
	{
		vk_destroy_texture(tex);
		return nullptr;
	}

	VkDescriptorImageInfo dii{};
	dii.sampler = tex->sampler;
	dii.imageView = tex->view;
	dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet wds{};
	wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	wds.dstSet = tex->descriptor_set;
	wds.dstBinding = 0;
	wds.descriptorCount = 1;
	wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	wds.pImageInfo = &dii;

	vkUpdateDescriptorSets(g_vk.device, 1, &wds, 0, nullptr);

	tex->valid = true;
	return tex;
}

void vk_destroy_texture(vk_texture *tex)
{
	if (!tex)
		return;

	if (tex->descriptor_set && g_vk.descriptor_pool)
		vkFreeDescriptorSets(g_vk.device, g_vk.descriptor_pool, 1, &tex->descriptor_set);
	if (tex->sampler)
		vkDestroySampler(g_vk.device, tex->sampler, nullptr);
	if (tex->view)
		vkDestroyImageView(g_vk.device, tex->view, nullptr);
	if (tex->image)
		vmaDestroyImage(g_vk.allocator, tex->image, tex->allocation);

	*tex = {};
}

void vk_bind_texture(vk_texture *tex)
{
	if (!tex || !tex->valid)
		g_vk.bound_texture = g_vk.white_texture.descriptor_set;
	else
		g_vk.bound_texture = tex->descriptor_set;
}

}  // namespace dcx

#endif  // DXX_USE_VULKAN
