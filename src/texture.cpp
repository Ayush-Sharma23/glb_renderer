#include "texture.hpp"

#include "stb_image.h"
#include <cmath>

#include <cstdio>
#include <cstring>

#include <vector>

Texture Texture::load_from_memory(VulkanContext& ctx, const uint8_t* data, size_t size,
    bool srgb)
{
    Texture tex;
    int w, h, comp;
    stbi_uc* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!pixels) {
        fprintf(stderr, "Failed to load texture from memory\n");
        return {};
    }

    tex.width = w;
    tex.height = h;
    tex.components = 4;

    VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    int mip_levels = (int)floor(log2(std::max(w, h))) + 1;

    tex.image = Image2D::create(ctx, w, h, fmt,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        mip_levels);

    tex.sampler = Sampler::create(ctx.device());

    if (tex.image.image) {
        upload_pixels(ctx, tex.image, pixels, (size_t)w * h * 4, true);
    }

    stbi_image_free(pixels);
    return tex;
}

void Texture::upload_pixels(VulkanContext& ctx, Image2D& img, const void* pixels,
    size_t pixel_size, bool generate_mips)
{
    VkDevice device = ctx.device();
    VkQueue queue = ctx.graphics_queue();
    uint32_t queue_family = ctx.graphics_queue_family();

    // Create staging buffer
    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = pixel_size;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer staging;
    VmaAllocation staging_alloc;
    VmaAllocationInfo staging_alloc_info;
    vmaCreateBuffer(ctx.allocator(), &buf_info, &alloc_info,
        &staging, &staging_alloc, &staging_alloc_info);

    void* mapped;
    vmaMapMemory(ctx.allocator(), staging_alloc, &mapped);
    memcpy(mapped, pixels, pixel_size);
    vmaUnmapMemory(ctx.allocator(), staging_alloc);

    // One-shot command buffer
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = queue_family;

    VkCommandPool pool;
    vkCreateCommandPool(device, &pool_info, nullptr, &pool);

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmd_info, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Transition to transfer dst optimal
    transition_layout(ctx, cmd, img.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        img.mip_levels, VK_IMAGE_ASPECT_COLOR_BIT);

    // Copy staging buffer to image
    VkBufferImageCopy copy_region{};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent = {(uint32_t)img.width, (uint32_t)img.height, 1};

    vkCmdCopyBufferToImage(cmd, staging, img.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

    if (generate_mips && img.mip_levels > 1) {
        // Generate mipmaps using vkCmdBlitImage
        for (int mip = 1; mip < img.mip_levels; ++mip) {
            int src_w = std::max(1, img.width >> (mip - 1));
            int src_h = std::max(1, img.height >> (mip - 1));
            int dst_w = std::max(1, img.width >> mip);
            int dst_h = std::max(1, img.height >> mip);

            // Transition previous mip to transfer src
            transition_layout(ctx, cmd, img.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                1, VK_IMAGE_ASPECT_COLOR_BIT);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip - 1;
            blit.srcSubresource.layerCount = 1;
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {src_w, src_h, 1};

            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip;
            blit.dstSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {dst_w, dst_h, 1};

            vkCmdBlitImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }

        // Transition last mip to transfer src
        int last = img.mip_levels - 1;
        int last_w = std::max(1, img.height >> last);
        int last_h = std::max(1, img.width >> last);
        (void)last_w; (void)last_h;

        // Transition all mips to shader readonly
        transition_layout(ctx, cmd, img.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            img.mip_levels, VK_IMAGE_ASPECT_COLOR_BIT);

        // Also transition the src mips
        for (int mip = 0; mip < img.mip_levels - 1; ++mip) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.image = img.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = mip;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    } else {
        // No mipmaps, transition directly
        transition_layout(ctx, cmd, img.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            img.mip_levels, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
    vkDestroyCommandPool(device, pool, nullptr);
    vmaDestroyBuffer(ctx.allocator(), staging, staging_alloc);
}

void Texture::transition_layout(VulkanContext& ctx, VkCommandBuffer cmd,
    VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
    int mip_levels, VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
