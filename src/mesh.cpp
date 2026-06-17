#include "mesh.hpp"

void GPUMesh::upload(VulkanContext& ctx, const GltfPrimitive& primitive) {
    vertex_count = primitive.vertex_count;
    index_count = primitive.index_count;
    material_index = primitive.material_index;

    size_t vertex_size = primitive.vertices.size() * sizeof(GltfVertex);
    size_t index_size = primitive.indices.size() * sizeof(uint32_t);

    // Create vertex buffer
    vertex_buffer = Buffer::create(ctx, vertex_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vertex_buffer.upload(ctx, primitive.vertices.data(), vertex_size);

    // Create index buffer
    index_buffer = Buffer::create(ctx, index_size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    index_buffer.upload(ctx, primitive.indices.data(), index_size);
}

void GPUMesh::destroy(VmaAllocator allocator) {
    vertex_buffer.destroy(allocator);
    index_buffer.destroy(allocator);
}
