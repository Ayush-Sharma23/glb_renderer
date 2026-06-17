#include "indirect_draw.hpp"
#include <cstring>

// VkDrawIndexedIndirect equivalent for Vulkan 1.4 compatibility
struct DrawIndexedIndirectCmd {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

void IndirectDrawManager::init(VulkanContext& ctx, int max_draws) {
    buffer = Buffer::create(
        ctx,
        sizeof(DrawIndexedIndirectCmd) * max_draws,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

void IndirectDrawManager::update(VulkanContext& ctx, const std::vector<IndirectDraw>& draws) {
    count = (int)draws.size();
    if (count == 0) return;

    DrawIndexedIndirectCmd* dst = (DrawIndexedIndirectCmd*)buffer.alloc_info.pMappedData;
    for (int i = 0; i < count; i++) {
        dst[i].indexCount = draws[i].index_count;
        dst[i].instanceCount = draws[i].instance_count;
        dst[i].firstIndex = draws[i].first_index;
        dst[i].vertexOffset = draws[i].vertex_offset;
        dst[i].firstInstance = draws[i].first_instance;
    }
}
