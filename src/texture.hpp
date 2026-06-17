#pragma once

#include "vulkan_context.hpp"
#include "sampler.hpp"
#include "image.hpp"

#include <vector>
#include <cstdint>

struct Texture {
    Image2D image;
    Sampler sampler;
    int width = 0;
    int height = 0;
    int components = 0;

    static Texture load_from_memory(VulkanContext& ctx, const uint8_t* data, size_t size,
        bool srgb = false);

    // Upload CPU pixel data to the GPU image with mipmap generation
    static void upload_pixels(VulkanContext& ctx, Image2D& img, const void* pixels,
        size_t pixel_size, bool generate_mips = true);

    // Transition image layout
    static void transition_layout(VulkanContext& ctx, VkCommandBuffer cmd,
        VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
        int mip_levels, VkImageAspectFlags aspect);

    void destroy(VulkanContext& ctx) {
        image.destroy(ctx);
        sampler.destroy(ctx.device());
    }
};
