#version 450

// Fullscreen triangle (no vertex buffer needed)
vec2 verts[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

layout(location = 0) out vec3 v_uv;

void main() {
    vec2 pos = verts[gl_VertexIndex];
    v_uv = vec3(pos, 1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
