#include "skin.hpp"
#include <cstring>

void SkinManager::init(VulkanContext& ctx, int max_joints) {
    VkDeviceSize size = sizeof(glm::mat4) * max_joints;
    bone_buffer = Buffer::create(
        ctx, size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

void SkinManager::upload(VulkanContext& ctx, const std::vector<glm::mat4>& bone_matrices) {
    if (bone_matrices.empty()) return;
    std::memcpy(bone_buffer.alloc_info.pMappedData, bone_matrices.data(),
        sizeof(glm::mat4) * bone_matrices.size());
}
