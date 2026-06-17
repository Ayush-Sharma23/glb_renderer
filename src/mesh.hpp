#pragma once

#include "vulkan_context.hpp"
#include "buffer.hpp"
#include "gltf_loader.hpp"

#include <vector>
#include <cstdint>

struct GPUMesh {
    Buffer vertex_buffer;
    Buffer index_buffer;
    int vertex_count = 0;
    int index_count = 0;
    int material_index = -1;

    void upload(VulkanContext& ctx, const GltfPrimitive& primitive);
    void destroy(VmaAllocator allocator);
};
