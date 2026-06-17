#include "image.hpp"

Image2D Image2D::create(VulkanContext& ctx, int w, int h, VkFormat fmt,
    VkImageUsageFlags usage, int mip_levels, VkImageAspectFlags aspect)
{
    Image2D img;
    img.width = w;
    img.height = h;
    img.format = fmt;
    img.mip_levels = mip_levels;

    VkExtent3D extent{(uint32_t)w, (uint32_t)h, 1};
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = fmt;
    info.extent = extent;
    info.mipLevels = mip_levels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(ctx.allocator(), &info, &alloc_info,
        &img.image, &img.allocation, nullptr) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create image %dx%d fmt=%d\n", w, h, fmt);
        return {};
    }

    VkImageViewCreateInfo iv{};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = img.image;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = fmt;
    iv.subresourceRange.aspectMask = aspect;
    iv.subresourceRange.levelCount = mip_levels;
    iv.subresourceRange.layerCount = 1;

    if (vkCreateImageView(ctx.device(), &iv, nullptr, &img.view) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create image view\n");
        vmaDestroyImage(ctx.allocator(), img.image, img.allocation);
        return {};
    }

    return img;
}

void Image2D::destroy(VulkanContext& ctx) {
    if (view) vkDestroyImageView(ctx.device(), view, nullptr);
    if (image) vmaDestroyImage(ctx.allocator(), image, allocation);
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
}
