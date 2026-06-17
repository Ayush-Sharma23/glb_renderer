#pragma once

#include "vulkan_context.hpp"
#include <vector>
#include <string>
#include <span>
#include <cstdint>

struct ShaderModule {
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage;

    static ShaderModule compile(VkDevice device, const std::string& spirv_path, VkShaderStageFlagBits stage);
    static ShaderModule from_spirv(VkDevice device, std::span<const uint32_t> code, VkShaderStageFlagBits stage);
    void destroy(VkDevice device);
};

struct GraphicsPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    struct Builder {
        VkDevice device;
        VkRenderPass render_pass;

        std::vector<ShaderModule> shaders;
        VkPipelineVertexInputStateCreateInfo vertex_input{};
        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        VkPipelineTessellationStateCreateInfo tessellation{};
        VkPipelineViewportStateCreateInfo viewport_state{};
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        VkPipelineMultisampleStateCreateInfo multisampling{};
        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        VkPipelineColorBlendStateCreateInfo color_blending{};
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        std::vector<VkDynamicState> dynamic_states;
        std::vector<VkPushConstantRange> push_constant_ranges;
        std::vector<VkDescriptorSetLayout> descriptor_set_layouts;

        Builder(VkDevice dev, VkRenderPass rp);

        Builder& add_shader(const ShaderModule& shader);
        Builder& add_vertex_input(std::span<VkVertexInputBindingDescription> bindings,
            std::span<VkVertexInputAttributeDescription> attributes);
        Builder& add_push_constant(VkShaderStageFlags stage, uint32_t size, uint32_t offset = 0);
        Builder& add_descriptor_set_layout(VkDescriptorSetLayout layout);
        Builder& set_cull_mode(VkCullModeFlags cull);
        Builder& set_depth_test(bool enable);
        Builder& set_depth_write(bool enable);
        Builder& set_depth_compare(VkCompareOp op);

        GraphicsPipeline build();
    };
};
