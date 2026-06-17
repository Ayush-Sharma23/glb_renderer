#pragma once

#include <vulkan/vulkan.h>

struct Sampler {
    VkSampler sampler = VK_NULL_HANDLE;

    static Sampler create(VkDevice device, VkFilter mag_filter = VK_FILTER_LINEAR,
        VkFilter min_filter = VK_FILTER_LINEAR,
        VkSamplerAddressMode address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        bool anisotropy = true, float max_anisotropy = 8.0f,
        bool mipmap = true);

    void destroy(VkDevice device);
};
