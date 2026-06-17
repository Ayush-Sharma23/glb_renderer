#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "gltf_loader.hpp"

struct AABB {
    glm::vec3 min = glm::vec3(1e30f);
    glm::vec3 max = glm::vec3(-1e30f);

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return (max - min) * 0.5f; }
    float radius() const { return glm::length(extents()); }
};

struct SceneNode {
    std::string name;
    glm::mat4 local_transform = glm::mat4(1.0f);
    glm::mat4 world_transform = glm::mat4(1.0f);
    int parent_index = -1;
    std::vector<int> children;
    std::vector<int> mesh_indices;
    int skin_index = -1;
    AABB bounds;
};

struct Drawable {
    int mesh_index;
    int primitive_index;
    int node_index;
    int material_index;
};

class Scene {
public:
    bool build_from_gltf(const GltfData& data);

    const std::vector<SceneNode>& nodes() const { return nodes_; }
    const std::vector<Drawable>& drawables() const { return drawables_; }
    const std::vector<int>& root_nodes() const { return root_nodes_; }
    const AABB& scene_bounds() const { return scene_bounds_; }

    void compute_world_transforms();

private:
    void compute_node_transform(int node_index, const glm::mat4& parent_world);

    std::vector<SceneNode> nodes_;
    std::vector<Drawable> drawables_;
    std::vector<int> root_nodes_;
    AABB scene_bounds_;
};
