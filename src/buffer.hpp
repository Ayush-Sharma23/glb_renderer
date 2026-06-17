#pragma once

#include "vulkan_context.hpp"

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo alloc_info{};
    VkDeviceSize size = 0;

    static Buffer create(VulkanContext& ctx, VkDeviceSize size,
        VkBufferUsageFlags usage, VmaMemoryUsage memory_usage,
        VkMemoryPropertyFlags required_flags = 0);

    void upload(VulkanContext& ctx, const void* data, VkDeviceSize data_size);
    void destroy(VmaAllocator allocator);

    VkDescriptorBufferInfo descriptor_info(VkDeviceSize offset = 0) const {
        VkDescriptorBufferInfo info{};
        info.buffer = buffer;
        info.offset = offset;
        info.range = size;
        return info;
    }
};

struct StagingBuffer {
    Buffer buffer;
    void* mapped = nullptr;

    static StagingBuffer create(VulkanContext& ctx, VkDeviceSize size);
    void destroy(VmaAllocator allocator);
    void copy_to(Buffer& dst, VkDeviceSize size, VkCommandBuffer cmd);
};
