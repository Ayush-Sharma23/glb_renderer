#version 450

// Irradiance convolution shader
// Renders to a cubemap face, convolves the environment map

layout(binding = 0) uniform samplerCube u_env;

layout(location = 0) in vec3 v_local;
layout(location = 0) out vec4 frag;

layout(push_constant) uniform Push {
    vec4 face_scale;
} pc;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(v_local);

    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    vec3 irradiance = vec3(0.0);

    uint sample_delta = 1u;
    float step = 1.0 / 64.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += step) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += step) {
            vec3 tangent_sample = vec3(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta)
            );
            vec3 sample_vec = T * tangent_sample.x + B * tangent_sample.y + N * tangent_sample.z;

            float NdotL = max(dot(N, sample_vec), 0.0);
            irradiance += texture(u_env, sample_vec).rgb * cos(theta) * sin(theta);
        }
    }

    irradiance *= PI / (64.0 * 64.0);
    irradiance = max(irradiance, 0.0);

    frag = vec4(irradiance, 1.0);
}
