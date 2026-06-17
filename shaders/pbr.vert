#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;
layout(location = 4) in uvec4 in_bone_indices;
layout(location = 5) in vec4 in_bone_weights;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view_proj;
    vec4 base_color_factor;
    vec4 emissive_factor;
    vec4 metallic_roughness;
    ivec4 tex_indices;
    ivec4 ibl_indices;
    vec4 light_dir;
    vec4 light_color;
    vec4 camera_pos;
} pc;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_tangent;

void main() {
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    gl_Position = pc.view_proj * world_pos;
    out_world_pos = world_pos.xyz;
    out_normal = normalize(mat3(pc.model) * in_normal);
    out_uv = in_uv;
    out_tangent = vec4(normalize(mat3(pc.model) * in_tangent.xyz), in_tangent.w);
}
