#pragma once

#include "vulkan_context.hpp"

struct Image2D {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    int width = 0;
    int height = 0;
    int mip_levels = 1;

    static Image2D create(VulkanContext& ctx, int w, int h, VkFormat fmt,
        VkImageUsageFlags usage, int mip_levels = 1,
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    void destroy(VulkanContext& ctx);
};
