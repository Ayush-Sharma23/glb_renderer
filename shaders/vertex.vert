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
} pc;

layout(location = 0) out vec3 out_color;

void main() {
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    gl_Position = pc.view_proj * world_pos;
    out_color = vec3(0.6, 0.7, 0.9); // flat blue-gray
}
