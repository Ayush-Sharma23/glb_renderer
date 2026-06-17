#include "pipeline.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

ShaderModule ShaderModule::compile(VkDevice device, const std::string& spirv_path, VkShaderStageFlagBits stage) {
    std::ifstream file(spirv_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "Failed to open shader: %s\n", spirv_path.c_str());
        return {};
    }

    size_t file_size = file.tellg();
    std::vector<uint32_t> code(file_size / sizeof(uint32_t));
    file.seekg(0);
    file.read((char*)code.data(), file_size);
    file.close();

    return from_spirv(device, code, stage);
}

ShaderModule ShaderModule::from_spirv(VkDevice device, std::span<const uint32_t> code, VkShaderStageFlagBits stage) {
    ShaderModule sm;
    sm.stage = stage;

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(uint32_t);
    info.pCode = code.data();

    if (vkCreateShaderModule(device, &info, nullptr, &sm.module) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module\n");
        return {};
    }

    return sm;
}

void ShaderModule::destroy(VkDevice device) {
    if (module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, module, nullptr);
        module = VK_NULL_HANDLE;
    }
}

GraphicsPipeline::Builder::Builder(VkDevice dev, VkRenderPass rp)
    : device(dev), render_pass(rp)
{
    // Default state values
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    color_blend_attachment.colorWriteMask = 0xF;
    color_blend_attachment.blendEnable = VK_FALSE;

    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (uint32_t)dynamic_states.size();
    dynamic_state.pDynamicStates = dynamic_states.data();
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::add_shader(const ShaderModule& shader) {
    shaders.push_back(shader);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::add_vertex_input(
    std::span<VkVertexInputBindingDescription> bindings,
    std::span<VkVertexInputAttributeDescription> attributes)
{
    vertex_input.vertexBindingDescriptionCount = (uint32_t)bindings.size();
    vertex_input.pVertexBindingDescriptions = bindings.data();
    vertex_input.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
    vertex_input.pVertexAttributeDescriptions = attributes.data();
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::add_push_constant(VkShaderStageFlags stage, uint32_t size, uint32_t offset) {
    push_constant_ranges.push_back({stage, offset, size});
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::add_descriptor_set_layout(VkDescriptorSetLayout layout) {
    descriptor_set_layouts.push_back(layout);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::set_cull_mode(VkCullModeFlags cull) {
    rasterizer.cullMode = cull;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::set_depth_test(bool enable) {
    depth_stencil.depthTestEnable = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::set_depth_write(bool enable) {
    depth_stencil.depthWriteEnable = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::set_depth_compare(VkCompareOp op) {
    depth_stencil.depthCompareOp = op;
    return *this;
}

GraphicsPipeline GraphicsPipeline::Builder::build() {
    GraphicsPipeline gp;

    // Pipeline layout
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = (uint32_t)descriptor_set_layouts.size();
    layout_info.pSetLayouts = descriptor_set_layouts.data();
    layout_info.pushConstantRangeCount = (uint32_t)push_constant_ranges.size();
    layout_info.pPushConstantRanges = push_constant_ranges.data();

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &gp.layout) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create pipeline layout\n");
        return {};
    }

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> stage_infos;
    for (auto& s : shaders) {
        VkPipelineShaderStageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage = s.stage;
        info.module = s.module;
        info.pName = "main";
        stage_infos.push_back(info);
    }

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = (uint32_t)stage_infos.size();
    info.pStages = stage_infos.data();
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisampling;
    info.pDepthStencilState = &depth_stencil;
    info.pColorBlendState = &color_blending;
    info.pDynamicState = &dynamic_state;
    info.layout = gp.layout;
    info.renderPass = render_pass;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &gp.pipeline) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create graphics pipeline\n");
        vkDestroyPipelineLayout(device, gp.layout, nullptr);
        return {};
    }

    // Destroy shader modules after pipeline creation
    for (auto& s : shaders) {
        s.destroy(device);
    }

    return gp;
}
