#include "buffer.hpp"

Buffer Buffer::create(VulkanContext& ctx, VkDeviceSize size,
    VkBufferUsageFlags usage, VmaMemoryUsage memory_usage,
    VkMemoryPropertyFlags required_flags)
{
    Buffer buf;
    buf.size = size;

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = size;
    buf_info.usage = usage;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = memory_usage;
    alloc_info.requiredFlags = required_flags;

    VmaAllocationInfo vma_info{};
    vmaCreateBuffer(ctx.allocator(), &buf_info, &alloc_info,
        &buf.buffer, &buf.allocation, &vma_info);
    buf.alloc_info = vma_info;

    return buf;
}

void Buffer::upload(VulkanContext& ctx, const void* data, VkDeviceSize data_size) {
    if (data_size > size) data_size = size;

    // Create staging buffer
    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = data_size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo staging_alloc{};
    staging_alloc.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer staging_buffer;
    VmaAllocation staging_allocation;
    VmaAllocationInfo staging_alloc_info;
    vmaCreateBuffer(ctx.allocator(), &staging_info, &staging_alloc,
        &staging_buffer, &staging_allocation, &staging_alloc_info);

    void* mapped;
    vmaMapMemory(ctx.allocator(), staging_allocation, &mapped);
    memcpy(mapped, data, data_size);
    vmaUnmapMemory(ctx.allocator(), staging_allocation);

    // One-time command buffer for copy
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = ctx.graphics_queue_family();

    VkCommandPool pool;
    vkCreateCommandPool(ctx.device(), &pool_info, nullptr, &pool);

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx.device(), &cmd_info, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkBufferCopy copy_region{};
    copy_region.size = data_size;
    vkCmdCopyBuffer(cmd, staging_buffer, buffer, 1, &copy_region);

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    vkQueueSubmit(ctx.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphics_queue());

    vkFreeCommandBuffers(ctx.device(), pool, 1, &cmd);
    vkDestroyCommandPool(ctx.device(), pool, nullptr);
    vmaDestroyBuffer(ctx.allocator(), staging_buffer, staging_allocation);
}

void Buffer::destroy(VmaAllocator allocator) {
    if (buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
    }
}
