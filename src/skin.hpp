#pragma once

#include "vulkan_context.hpp"
#include "buffer.hpp"
#include <glm/glm.hpp>
#include <vector>

struct SkinManager {
    Buffer bone_buffer;

    void init(VulkanContext& ctx, int max_joints);
    void upload(VulkanContext& ctx, const std::vector<glm::mat4>& bone_matrices);
    void destroy(VmaAllocator a) { bone_buffer.destroy(a); }
};
