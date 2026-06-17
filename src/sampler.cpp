#include "sampler.hpp"
#include <cstdio>

Sampler Sampler::create(VkDevice device, VkFilter mag, VkFilter min,
    VkSamplerAddressMode address_mode, bool anisotropy, float max_aniso, bool mipmap)
{
    Sampler s;
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = mag;
    info.minFilter = min;
    info.addressModeU = address_mode;
    info.addressModeV = address_mode;
    info.addressModeW = address_mode;
    info.anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = max_aniso;
    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;

    if (mipmap) {
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.minLod = 0.0f;
        info.maxLod = VK_LOD_CLAMP_NONE;
        info.mipLodBias = 0.0f;
    }

    if (vkCreateSampler(device, &info, nullptr, &s.sampler) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create sampler\n");
        return {};
    }
    return s;
}

void Sampler::destroy(VkDevice device) {
    if (sampler) vkDestroySampler(device, sampler, nullptr);
    sampler = VK_NULL_HANDLE;
}
