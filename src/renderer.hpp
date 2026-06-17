#pragma once

#include "vulkan_context.hpp"
#include "gltf_loader.hpp"
#include "scene.hpp"
#include "mesh.hpp"
#include "pipeline.hpp"
#include "camera.hpp"
#include "texture.hpp"
#include "material.hpp"
#include "descriptor_set.hpp"
#include "frustum.hpp"
#include "indirect_draw.hpp"
#include "ibl.hpp"
#include "profiler.hpp"

#include <vector>
#include <memory>

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool init(const char* glb_path);
    void run();
    void cleanup();
    const GltfData& gltf_data() const { return gltf_data_; }

private:
    bool load_scene(const char* path);
    bool create_pipelines();
    bool upload_meshes();
    bool load_textures();
    bool create_bindless_descriptors();
    bool create_ibl();

    void render_frame(float time);

    VulkanContext ctx_;

    GltfData gltf_data_;
    Scene scene_;
    std::vector<GPUMesh> gpu_meshes_;
    std::vector<int> mesh_primitive_offset_; // maps mesh_idx → gpu_meshes_ start index

    GraphicsPipeline flat_pipeline_;
    GraphicsPipeline pbr_pipeline_;
    GraphicsPipeline skybox_pipeline_;

    Camera camera_;
    FrameProfiler profiler_;
    Frustum frustum_;

    BindlessDescriptorManager bindless_descriptors_;
    std::vector<Texture> textures_;
    std::vector<PBRMaterial> materials_;

    IBLData ibl_;
    IndirectDrawManager indirect_;

    VkViewport viewport_{};
    VkRect2D scissor_{};
};
