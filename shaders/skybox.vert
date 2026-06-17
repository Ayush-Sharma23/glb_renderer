#version 450

layout(location = 0) out vec3 v_local;

layout(push_constant) uniform Push {
    mat4 view_proj;
} pc;

void main() {
    // Fullscreen triangle
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 pos = uv * 2.0 - 1.0;
    gl_Position = vec4(pos, 1.0, 1.0);

    // Reconstruct world direction (inverse VP, z=1)
    mat4 inv_vp = inverse(pc.view_proj);
    vec4 dir = inv_vp * vec4(pos, 1.0, 1.0);
    v_local = dir.xyz / dir.w;
}
