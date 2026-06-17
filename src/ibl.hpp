#pragma once

#include "vulkan_context.hpp"
#include "image.hpp"
#include "buffer.hpp"
#include "sampler.hpp"
#include <vector>

struct IBLData {
    // BRDF LUT (2D RG16F texture)
    Image2D brdf_lut_img;
    VkImageView brdf_lut_view = VK_NULL_HANDLE;

    // Cubemaps (raw handles since Image2D doesn't support cubemap)
    VkImage skybox = VK_NULL_HANDLE;
    VmaAllocation skybox_alloc = VK_NULL_HANDLE;
    VkImageView skybox_view = VK_NULL_HANDLE;

    VkImage irradiance = VK_NULL_HANDLE;
    VmaAllocation irradiance_alloc = VK_NULL_HANDLE;
    VkImageView irradiance_view = VK_NULL_HANDLE;

    VkImage prefiltered = VK_NULL_HANDLE;
    VmaAllocation prefiltered_alloc = VK_NULL_HANDLE;
    VkImageView prefiltered_view = VK_NULL_HANDLE;

    Sampler cubemap_sampler;

    bool build(VulkanContext& ctx);
    void destroy(VulkanContext& ctx);
};
