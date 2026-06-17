#pragma once

#include <glm/glm.hpp>

struct PBRMaterial {
    glm::vec4 base_color_factor = glm::vec4(1.0f);
    glm::vec3 emissive_factor = glm::vec3(0.0f);
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    float alpha_cutoff = 0.5f;
    int alpha_mode = 0; // 0=OPAQUE, 1=MASK, 2=BLEND
    bool double_sided = false;

    // Bindless texture indices (-1 = none)
    int base_color_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int occlusion_texture = -1;
    int emissive_texture = -1;
};

struct PBRMaterialUBO {
    glm::vec4 base_color_factor;
    glm::vec4 emissive_factor;
    glm::vec4 metallic_roughness; // x=metallic, y=roughness, z=alpha_cutoff, w=alpha_mode
    glm::ivec4 tex_indices;       // x=base_color, y=mr, z=normal, w=occlusion
    glm::ivec4 ibl_indices;       // x=emissive, y=irradiance, z=prefiltered, w=brdf_lut
    glm::vec4 light_dir;          // xyz=direction, w=pad
    glm::vec4 light_color;        // xyz=color, w=intensity
};
