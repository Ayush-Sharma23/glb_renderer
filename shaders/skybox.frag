#version 450

// Procedural gradient skybox — no external texture needed

layout(location = 0) in vec3 v_dir;
layout(location = 0) out vec4 frag;

void main() {
    vec3 d = normalize(v_dir);
    float horizon = max(0.0, -d.y * 0.5 + 0.5);
    float sky = max(0.0, d.y * 0.7 + 0.3);
    vec3 color = vec3(
        0.35 + horizon * 0.15 + sky * 0.25,
        0.35 + horizon * 0.15 + sky * 0.45,
        0.50 + horizon * 0.10 + sky * 0.70
    );
    frag = vec4(color, 1.0);
}
