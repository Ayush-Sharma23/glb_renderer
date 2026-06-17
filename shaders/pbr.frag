#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(set = 0, binding = 1) uniform samplerCube cubemaps[];
layout(set = 0, binding = 2) uniform sampler2D extra_2d[];

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

float ggx_distribution(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * denom * denom);
}

float smith_ggx(float NdotV, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotV + sqrt(NdotV * (NdotV - a2 * NdotV) + a2);
    return 2.0 * NdotV / denom;
}

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cos_theta, 5.0);
}

vec3 fresnel_schlick_roughness(float cos_theta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cos_theta, 5.0);
}

vec3 calc_light(vec3 L, vec3 light_col, float intensity, vec3 N, vec3 V, vec3 F0, float metallic, float roughness, vec3 diffuse) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.001);
    float NdotH = max(dot(N, H), 0.001);
    float HdotV = max(dot(H, V), 0.001);
    float NdotV = max(dot(N, V), 0.001);

    float D = ggx_distribution(NdotH, roughness);
    float G = smith_ggx(NdotV, roughness) * smith_ggx(NdotL, roughness);
    vec3 F = fresnel_schlick(HdotV, F0);
    vec3 specular = D * G * F / (4.0 * NdotV * NdotL + 0.0001);

    return (diffuse + specular) * light_col * intensity * NdotL;
}

void main() {
    vec4 base_color = pc.base_color_factor;
    if (pc.tex_indices.x >= 0)
        base_color *= texture(textures[pc.tex_indices.x], in_uv);

    float metallic = pc.metallic_roughness.x;
    float roughness = pc.metallic_roughness.y;
    if (pc.tex_indices.y >= 0) {
        vec4 mr = texture(textures[pc.tex_indices.y], in_uv);
        metallic *= mr.b;
        roughness *= mr.g;
    }

    if (pc.metallic_roughness.w > 0.5 && base_color.a < pc.metallic_roughness.z)
        discard;

    vec3 N = normalize(in_normal);

    if (pc.tex_indices.z >= 0) {
        vec3 tangent = normalize(in_tangent.xyz);
        vec3 bitangent = cross(N, tangent) * in_tangent.w;
        mat3 TBN = mat3(tangent, bitangent, N);
        vec3 nm = texture(textures[pc.tex_indices.z], in_uv).xyz;
        nm = nm * 2.0 - 1.0;
        N = normalize(TBN * nm);
    }

    vec3 V = normalize(pc.camera_pos.xyz - in_world_pos);
    float NdotV = max(dot(N, V), 0.001);
    vec3 F0 = mix(vec3(0.04), base_color.rgb, metallic);
    vec3 diffuse = (1.0 - metallic) * base_color.rgb / 3.14159;

    vec3 direct = vec3(0.0);
    direct += calc_light(normalize(pc.light_dir.xyz), pc.light_color.rgb, pc.light_color.w, N, V, F0, metallic, roughness, diffuse);

    vec3 irradiance_color = vec3(0.3);
    if (pc.ibl_indices.y >= 0)
        irradiance_color = texture(cubemaps[pc.ibl_indices.y], N).rgb;
    vec3 ambient = (1.0 - metallic) * base_color.rgb * irradiance_color * 0.3;

    vec3 ibl_specular = vec3(0.4);
    if (pc.ibl_indices.z >= 0 && pc.ibl_indices.w >= 0) {
        vec3 R = reflect(-V, N);
        float lod = roughness * 4.0;
        vec3 prefiltered_color = textureLod(cubemaps[pc.ibl_indices.z], R, lod).rgb;
        vec2 brdf = texture(extra_2d[pc.ibl_indices.w], vec2(NdotV, roughness)).rg;
        ibl_specular = prefiltered_color * (F0 * brdf.x + brdf.y) * 0.5;
    }

    float ao = 1.0;
    if (pc.tex_indices.w >= 0)
        ao = texture(textures[pc.tex_indices.w], in_uv).r;

    vec3 emissive = pc.emissive_factor.rgb;
    if (pc.ibl_indices.x >= 0)
        emissive *= texture(textures[pc.ibl_indices.x], in_uv).rgb;

    vec3 color = direct + (ambient + ibl_specular) * ao + emissive;

    // ACES filmic tone mapping (Narkowicz 2015 fit)
    color = clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);

    out_color = vec4(color, base_color.a);
}
