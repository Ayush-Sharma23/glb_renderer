#include "renderer.hpp"
#include "shader_path.hpp"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cmath>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    Camera* cam = (Camera*)glfwGetWindowUserPointer(window);
    if (cam) cam->on_scroll(yoffset);
}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::init(const char* glb_path) {
    if (!ctx_.init()) {
        fprintf(stderr, "Failed to initialize Vulkan context\n");
        return false;
    }

    // Register scroll callback for the camera
    glfwSetWindowUserPointer(ctx_.window(), &camera_);
    glfwSetScrollCallback(ctx_.window(), scroll_callback);

    if (!load_scene(glb_path)) return false;
    if (!upload_meshes()) return false;
    if (!load_textures()) return false;
    if (!create_bindless_descriptors()) return false;
    if (!create_ibl()) return false;
    if (!create_pipelines()) return false;

    indirect_.init(ctx_, (int)gpu_meshes_.size());

    float scene_radius = scene_.scene_bounds().radius();
    camera_.set_target(scene_.scene_bounds().center());
    camera_.set_distance(std::max(scene_radius * 3.0f, 0.5f));

    return true;
}

bool Renderer::load_scene(const char* path) {
    GltfLoader loader;
    if (!loader.load(path)) return false;
    gltf_data_ = loader.data();
    scene_.build_from_gltf(gltf_data_);
    return true;
}

bool Renderer::upload_meshes() {
    mesh_primitive_offset_.clear();
    int gpu_idx = 0;
    for (auto& mesh : gltf_data_.meshes) {
        mesh_primitive_offset_.push_back(gpu_idx);
        for (auto& prim : mesh.primitives) {
            GPUMesh gpu_mesh;
            gpu_mesh.upload(ctx_, prim);
            gpu_meshes_.push_back(std::move(gpu_mesh));
            gpu_idx++;
        }
    }
    printf("Uploaded %zu GPU meshes\n", gpu_meshes_.size());
    return true;
}

bool Renderer::load_textures() {
    textures_.reserve(gltf_data_.textures.size());
    materials_.reserve(gltf_data_.materials.size());

    int slot = 0;
    std::unordered_map<int, int> tex_slot_map;

    // Build material array with texture indices
    for (auto& gm : gltf_data_.materials) {
        PBRMaterial mat;
        mat.base_color_factor = gm.base_color_factor;
        mat.emissive_factor = gm.emissive_factor;
        mat.metallic_factor = gm.metallic_factor;
        mat.roughness_factor = gm.roughness_factor;
        mat.alpha_cutoff = gm.alpha_cutoff;
        mat.alpha_mode = gm.alpha_mode;
        mat.double_sided = gm.double_sided;

        auto get_or_create_tex = [&](int tex_idx, bool srgb) -> int {
            if (tex_idx < 0) return -1;
            auto it = tex_slot_map.find(tex_idx);
            if (it != tex_slot_map.end()) return it->second;

            int current_slot = slot;
            auto& gt = gltf_data_.textures[tex_idx];

            if (!gt.data.empty()) {
                Texture tex = Texture::load_from_memory(ctx_, gt.data.data(), gt.data.size(), srgb);
                textures_.push_back(std::move(tex));
            } else {
                uint32_t white = 0xFFFFFFFF;
                Texture tex;
                tex.width = 1; tex.height = 1; tex.components = 4;
                tex.image = Image2D::create(ctx_, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1);
                tex.sampler = Sampler::create(ctx_.device());
                if (tex.image.image)
                    Texture::upload_pixels(ctx_, tex.image, &white, 4, false);
                textures_.push_back(std::move(tex));
            }

            tex_slot_map[tex_idx] = current_slot;
            slot++;
            return current_slot;
        };

        mat.base_color_texture = get_or_create_tex(gm.base_color_texture, true);
        mat.metallic_roughness_texture = get_or_create_tex(gm.metallic_roughness_texture, false);
        mat.normal_texture = get_or_create_tex(gm.normal_texture, false);
        mat.occlusion_texture = get_or_create_tex(gm.occlusion_texture, false);
        mat.emissive_texture = get_or_create_tex(gm.emissive_texture, true);

        materials_.push_back(mat);
    }

    printf("Loaded %zu textures, %zu materials\n", textures_.size(), materials_.size());
    return true;
}
    return true;
}

bool Renderer::create_bindless_descriptors() {
    bindless_descriptors_.init(ctx_);

    // Add all textures to the bindless array
    for (int i = 0; i < (int)textures_.size(); ++i) {
        bindless_descriptors_.add_texture(ctx_,
            textures_[i].image.view, textures_[i].sampler.sampler, i);
    }

    printf("Bindless descriptors: %zu textures\n", textures_.size());
    return true;
}

bool Renderer::create_ibl() {
    if (!ibl_.build(ctx_)) {
        fprintf(stderr, "Failed to generate IBL data\n");
        return false;
    }

    // Add IBL resources to bindless descriptor set
    bindless_descriptors_.add_cubemap(ctx_,
        ibl_.irradiance_view, ibl_.cubemap_sampler.sampler,
        BindlessDescriptorManager::IRRADIANCE_SLOT);
    bindless_descriptors_.add_cubemap(ctx_,
        ibl_.prefiltered_view, ibl_.cubemap_sampler.sampler,
        BindlessDescriptorManager::PREFILTERED_SLOT);
    bindless_descriptors_.add_extra_2d(ctx_,
        ibl_.brdf_lut_view, ibl_.cubemap_sampler.sampler,
        BindlessDescriptorManager::BRDF_LUT_SLOT);

    return true;
}

bool Renderer::create_pipelines() {
    auto vert_flat = ShaderModule::compile(ctx_.device(),
        shader_path("vertex.vert.spv"), VK_SHADER_STAGE_VERTEX_BIT);
    auto frag_flat = ShaderModule::compile(ctx_.device(),
        shader_path("fragment.frag.spv"), VK_SHADER_STAGE_FRAGMENT_BIT);
    auto vert_pbr = ShaderModule::compile(ctx_.device(),
        shader_path("pbr.vert.spv"), VK_SHADER_STAGE_VERTEX_BIT);
    auto frag_pbr = ShaderModule::compile(ctx_.device(),
        shader_path("pbr.frag.spv"), VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!vert_flat.module || !frag_flat.module || !vert_pbr.module || !frag_pbr.module) {
        fprintf(stderr, "Failed to compile shaders\n");
        return false;
    }

    // Vertex input matching GltfVertex
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(GltfVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributes(6);
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, normal)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GltfVertex, uv)};
    attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GltfVertex, tangent)};
    attributes[4] = {4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(GltfVertex, bone_indices)};
    attributes[5] = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GltfVertex, bone_weights)};

    VkVertexInputBindingDescription bindings_arr[1] = {binding};
    std::span bindings_span = bindings_arr;
    std::span attributes_span = attributes;

    // Flat color pipeline (Stage 2)
    flat_pipeline_ = GraphicsPipeline::Builder(ctx_.device(), ctx_.render_pass())
        .add_shader(vert_flat)
        .add_shader(frag_flat)
        .add_vertex_input(bindings_span, attributes_span)
        .add_push_constant(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4) * 2)
        .set_cull_mode(VK_CULL_MODE_NONE)
        .set_depth_test(true)
        .set_depth_write(true)
        .build();

    if (!flat_pipeline_.pipeline) {
        fprintf(stderr, "Failed to create flat pipeline\n");
        return false;
    }

    // Unified push constant for PBR (shared between vert and frag)
    struct alignas(16) PBRPushConstant {
        glm::mat4 model;
        glm::mat4 view_proj;
        glm::vec4 base_color_factor;
        glm::vec4 emissive_factor;
        glm::vec4 metallic_roughness;
        glm::ivec4 tex_indices;
        glm::ivec4 ibl_indices;
        glm::vec4 light_dir;
        glm::vec4 light_color;
        glm::vec4 camera_pos;
    };
    static_assert(sizeof(PBRPushConstant) <= 256, "Push constant exceeds 256-byte limit");

    // PBR pipeline
    pbr_pipeline_ = GraphicsPipeline::Builder(ctx_.device(), ctx_.render_pass())
        .add_shader(vert_pbr)
        .add_shader(frag_pbr)
        .add_vertex_input(bindings_span, attributes_span)
        .add_descriptor_set_layout(bindless_descriptors_.get_layout())
        .add_push_constant(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PBRPushConstant))
        .set_cull_mode(VK_CULL_MODE_NONE)
        .set_depth_test(true)
        .set_depth_write(true)
        .build();

    if (!pbr_pipeline_.pipeline) {
        fprintf(stderr, "Failed to create PBR pipeline\n");
        return false;
    }

    // Skybox pipeline
    auto vert_sky = ShaderModule::compile(ctx_.device(),
        shader_path("skybox.vert.spv"), VK_SHADER_STAGE_VERTEX_BIT);
    auto frag_sky = ShaderModule::compile(ctx_.device(),
        shader_path("skybox.frag.spv"), VK_SHADER_STAGE_FRAGMENT_BIT);
    if (vert_sky.module && frag_sky.module) {
        skybox_pipeline_ = GraphicsPipeline::Builder(ctx_.device(), ctx_.render_pass())
            .add_shader(vert_sky)
            .add_shader(frag_sky)
            .add_vertex_input({}, {})
            .add_push_constant(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4))
            .set_cull_mode(VK_CULL_MODE_FRONT_BIT)
            .set_depth_test(true)
            .set_depth_write(false)
            .set_depth_compare(VK_COMPARE_OP_LESS_OR_EQUAL)
            .build();
        if (!skybox_pipeline_.pipeline) {
            fprintf(stderr, "Warning: Failed to create skybox pipeline\n");
        }
    } else {
        fprintf(stderr, "Warning: Failed to compile skybox shaders\n");
    }

    printf("Pipelines created\n");
    return true;
}

void Renderer::run() {
    double last_time = glfwGetTime();
    while (!glfwWindowShouldClose(ctx_.window())) {
        glfwPollEvents();

        if (glfwGetKey(ctx_.window(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(ctx_.window(), GLFW_TRUE);

        double now = glfwGetTime();
        double dt = now - last_time;
        last_time = now;
        profiler_.tick(dt);

        camera_.update(dt, ctx_.window());
        camera_.set_aspect((float)ctx_.window_extent().width / (float)ctx_.window_extent().height);

        if (ctx_.begin_frame()) {
            render_frame((float)now);
            ctx_.end_frame();
            ctx_.submit_and_present();
        }

        if (profiler_.fps() > 0) {
            char title[64];
            snprintf(title, sizeof(title), "GLB Renderer - %d FPS", profiler_.fps());
            glfwSetWindowTitle(ctx_.window(), title);
        }
    }

    vkDeviceWaitIdle(ctx_.device());
}

void Renderer::render_frame(float time) {
    auto& frame = ctx_.current_frame();
    auto& cmd = frame.command_buffer;
    auto extent = ctx_.window_extent();

    viewport_ = {0, 0, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    scissor_ = {{0, 0}, extent};

    auto view = camera_.view();
    auto proj = camera_.projection();
    glm::mat4 vp = proj * view;

    // Extract frustum for culling
    frustum_.extract(vp);

    VkClearValue clear_values[2];
    clear_values[0].color = {{0.1f, 0.1f, 0.12f, 1.0f}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = ctx_.render_pass();
    rp_begin.framebuffer = ctx_.swapchain().framebuffers[ctx_.image_index()];
    rp_begin.renderArea = scissor_;
    rp_begin.clearValueCount = 2;
    rp_begin.pClearValues = clear_values;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport_);
    vkCmdSetScissor(cmd, 0, 1, &scissor_);

    // Frustum culling + draw
    for (auto& drawable : scene_.drawables()) {
        auto& node = scene_.nodes()[drawable.node_index];

        // Look up the correct GPU mesh using (mesh_index, primitive_index)
        int gpu_idx = mesh_primitive_offset_[drawable.mesh_index] + drawable.primitive_index;
        auto& gpu_mesh = gpu_meshes_[gpu_idx];

        if (frustum_.test_aabb(node.bounds.min, node.bounds.max)) {
            if (drawable.material_index >= 0 && drawable.material_index < (int)materials_.size()) {
                auto& mat = materials_[drawable.material_index];
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pbr_pipeline_.pipeline);

                VkDescriptorSet ds = bindless_descriptors_.get_set();
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pbr_pipeline_.layout, 0, 1, &ds, 0, nullptr);

                struct alignas(16) PushData {
                    glm::mat4 model;
                    glm::mat4 view_proj;
                    glm::vec4 base_color_factor;
                    glm::vec4 emissive_factor;
                    glm::vec4 metallic_roughness;
                    glm::ivec4 tex_indices;
                    glm::ivec4 ibl_indices;
                    glm::vec4 light_dir;
                    glm::vec4 light_color;
                    glm::vec4 camera_pos;
                } push;

                push.model = node.world_transform;
                push.view_proj = vp;
                push.base_color_factor = mat.base_color_factor;
                push.emissive_factor = glm::vec4(mat.emissive_factor, 1.0f);
                push.metallic_roughness = glm::vec4(mat.metallic_factor, mat.roughness_factor,
                    mat.alpha_cutoff, (float)mat.alpha_mode);
                push.tex_indices = glm::ivec4(
                    mat.base_color_texture,
                    mat.metallic_roughness_texture,
                    mat.normal_texture,
                    mat.occlusion_texture);
                push.ibl_indices = glm::ivec4(
                    mat.emissive_texture,
                    BindlessDescriptorManager::IRRADIANCE_SLOT,
                    BindlessDescriptorManager::PREFILTERED_SLOT,
                    BindlessDescriptorManager::BRDF_LUT_SLOT);
                push.light_dir = glm::vec4(glm::normalize(glm::vec3(
                    std::cos(time * 0.5f) * 1.5f,
                    2.0f,
                    std::sin(time * 0.5f) * 1.5f)), 0.0f);
                push.light_color = glm::vec4(1.0f, 0.95f, 0.9f, 2.5f);
                push.camera_pos = glm::vec4(camera_.position(), 1.0f);

                vkCmdPushConstants(cmd, pbr_pipeline_.layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(push), &push);
            } else {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flat_pipeline_.pipeline);

                glm::mat4 push_data[2] = {node.world_transform, vp};
                vkCmdPushConstants(cmd, flat_pipeline_.layout,
                    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
            }

            VkDeviceSize offsets[1] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, &gpu_mesh.vertex_buffer.buffer, offsets);
            vkCmdBindIndexBuffer(cmd, gpu_mesh.index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, gpu_mesh.index_count, 1, 0, 0, 0);
        }
    }

    // Skybox overlay (if pipeline created)
    if (skybox_pipeline_.pipeline) {
        glm::mat4 sky_view = glm::mat4(glm::mat3(view));
        glm::mat4 sky_vp = proj * sky_view;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skybox_pipeline_.pipeline);
        vkCmdPushConstants(cmd, skybox_pipeline_.layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(sky_vp), &sky_vp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void Renderer::cleanup() {
    for (auto& tex : textures_) tex.destroy(ctx_);
    for (auto& mesh : gpu_meshes_) mesh.destroy(ctx_.allocator());

    bindless_descriptors_.destroy(ctx_.device());

    auto destroy_pipeline = [&](GraphicsPipeline& p) {
        if (p.pipeline) vkDestroyPipeline(ctx_.device(), p.pipeline, nullptr);
        if (p.layout) vkDestroyPipelineLayout(ctx_.device(), p.layout, nullptr);
    };
    destroy_pipeline(flat_pipeline_);
    destroy_pipeline(pbr_pipeline_);
    destroy_pipeline(skybox_pipeline_);
    destroy_pipeline(skybox_pipeline_);

    indirect_.destroy(ctx_.allocator());
    ibl_.destroy(ctx_);

    ctx_.cleanup();
}
