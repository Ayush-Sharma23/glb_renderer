#pragma once

#include "vulkan_context.hpp"
#include <vector>

struct BindlessDescriptorManager {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;

    static constexpr int MAX_TEXTURES = 1024;
    static constexpr int MAX_CUBEMAPS = 16;
    static constexpr int MAX_EXTRA_2D = 16;

    void init(VulkanContext& ctx);
    void add_texture(VulkanContext& ctx, VkImageView image_view, VkSampler sampler, int slot);
    void add_cubemap(VulkanContext& ctx, VkImageView image_view, VkSampler sampler, int slot);
    void add_extra_2d(VulkanContext& ctx, VkImageView image_view, VkSampler sampler, int slot);
    void destroy(VkDevice device);

    VkDescriptorSetLayout get_layout() const { return layout; }
    VkDescriptorSet get_set() const { return set; }

    // Convenience: assign known slots for IBL resources
    static constexpr int IRRADIANCE_SLOT = 0;
    static constexpr int PREFILTERED_SLOT = 1;
    static constexpr int BRDF_LUT_SLOT = 0;
};
