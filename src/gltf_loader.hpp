#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

struct GltfVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
    glm::uvec4 bone_indices;
    glm::vec4 bone_weights;
};

struct GltfPrimitive {
    std::vector<GltfVertex> vertices;
    std::vector<uint32_t> indices;
    int material_index;
    int vertex_count;
    int index_count;
};

struct GltfMesh {
    std::string name;
    std::vector<GltfPrimitive> primitives;
    glm::vec3 min_pos;
    glm::vec3 max_pos;
};

struct GltfTexture {
    std::string name;
    std::string uri;
    int width;
    int height;
    int components;
    std::vector<uint8_t> data;
};

struct GltfMaterial {
    std::string name;
    glm::vec4 base_color_factor;
    glm::vec3 emissive_factor;
    float metallic_factor;
    float roughness_factor;
    float alpha_cutoff;
    int alpha_mode; // 0=OPAQUE, 1=MASK, 2=BLEND
    bool double_sided;
    int base_color_texture;
    int metallic_roughness_texture;
    int normal_texture;
    int occlusion_texture;
    int emissive_texture;
};

struct GltfNode {
    std::string name;
    glm::mat4 local_transform;
    std::vector<int> children;
    std::vector<int> mesh_indices;
    int skin_index;
    int parent_index;
};

struct GltfAnimationChannel {
    int target_node;
    std::string target_path;
    std::vector<float> times;
    std::vector<glm::vec4> values;
};

struct GltfAnimation {
    std::string name;
    std::vector<GltfAnimationChannel> channels;
};

struct GltfSkin {
    std::string name;
    std::vector<int> joint_nodes;
    std::vector<glm::mat4> inverse_bind_matrices;
    int skeleton_root;
};

struct GltfScene {
    std::string name;
    std::vector<int> root_nodes;
};

struct GltfData {
    std::vector<GltfMesh> meshes;
    std::vector<GltfTexture> textures;
    std::vector<GltfMaterial> materials;
    std::vector<GltfNode> nodes;
    std::vector<GltfScene> scenes;
    std::vector<GltfAnimation> animations;
    std::vector<GltfSkin> skins;
    int default_scene;

    void dump() const;
};

class GltfLoader {
public:
    bool load(const std::string& path);
    const GltfData& data() const { return data_; }
    void cleanup();

private:
    GltfData data_;
};
