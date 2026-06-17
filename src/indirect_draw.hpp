#pragma once

#include "vulkan_context.hpp"
#include "buffer.hpp"
#include <vector>
#include <cstdint>

struct IndirectDraw {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
};

struct IndirectDrawManager {
    Buffer buffer;
    int count = 0;

    void init(VulkanContext& ctx, int max_draws);
    void update(VulkanContext& ctx, const std::vector<IndirectDraw>& draws);
    void destroy(VmaAllocator allocator) { buffer.destroy(allocator); }
};
