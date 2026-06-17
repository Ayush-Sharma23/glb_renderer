#include "scene.hpp"
#include <cstdio>

bool Scene::build_from_gltf(const GltfData& data) {
    nodes_.reserve(data.nodes.size());

    for (size_t i = 0; i < data.nodes.size(); ++i) {
        auto& gn = data.nodes[i];
        SceneNode sn;
        sn.name = gn.name;
        sn.local_transform = gn.local_transform;
        sn.parent_index = gn.parent_index;
        sn.children = gn.children;
        sn.mesh_indices = gn.mesh_indices;
        sn.skin_index = gn.skin_index;

        // Compute bounds from referenced meshes
        for (int mi : gn.mesh_indices) {
            if (mi >= 0 && mi < (int)data.meshes.size()) {
                auto& mesh = data.meshes[mi];
                sn.bounds.min = glm::min(sn.bounds.min, mesh.min_pos);
                sn.bounds.max = glm::max(sn.bounds.max, mesh.max_pos);
            }
        }

        nodes_.push_back(sn);
    }

    // Find root nodes (those without a parent)
    root_nodes_.clear();
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].parent_index < 0) {
            root_nodes_.push_back((int)i);
        }
    }

    // Build drawable list
    drawables_.clear();
    for (int ni = 0; ni < (int)nodes_.size(); ++ni) {
        for (int mi : nodes_[ni].mesh_indices) {
            if (mi >= 0 && mi < (int)data.meshes.size()) {
                for (int pi = 0; pi < (int)data.meshes[mi].primitives.size(); ++pi) {
                    Drawable d;
                    d.mesh_index = mi;
                    d.primitive_index = pi;
                    d.node_index = ni;
                    d.material_index = data.meshes[mi].primitives[pi].material_index;
                    drawables_.push_back(d);
                }
            }
        }
    }

    printf("Scene: %zu nodes, %zu drawables\n", nodes_.size(), drawables_.size());

    compute_world_transforms();

    // Compute scene-level bounds
    scene_bounds_ = AABB{};
    if (!drawables_.empty()) {
        for (auto& d : drawables_) {
            auto& node = nodes_[d.node_index];
            scene_bounds_.min = glm::min(scene_bounds_.min, node.bounds.min);
            scene_bounds_.max = glm::max(scene_bounds_.max, node.bounds.max);
        }
    }

    return true;
}

void Scene::compute_world_transforms() {
    for (int root : root_nodes_) {
        compute_node_transform(root, glm::mat4(1.0f));
    }
}

void Scene::compute_node_transform(int node_index, const glm::mat4& parent_world) {
    auto& node = nodes_[node_index];
    node.world_transform = parent_world * node.local_transform;

    // Transform bounds into world space
    AABB world_bounds;
    glm::vec3 corners[8] = {
        glm::vec3(node.bounds.min.x, node.bounds.min.y, node.bounds.min.z),
        glm::vec3(node.bounds.max.x, node.bounds.min.y, node.bounds.min.z),
        glm::vec3(node.bounds.min.x, node.bounds.max.y, node.bounds.min.z),
        glm::vec3(node.bounds.max.x, node.bounds.max.y, node.bounds.min.z),
        glm::vec3(node.bounds.min.x, node.bounds.min.y, node.bounds.max.z),
        glm::vec3(node.bounds.max.x, node.bounds.min.y, node.bounds.max.z),
        glm::vec3(node.bounds.min.x, node.bounds.max.y, node.bounds.max.z),
        glm::vec3(node.bounds.max.x, node.bounds.max.y, node.bounds.max.z),
    };
    for (auto& c : corners) {
        glm::vec4 tc = node.world_transform * glm::vec4(c, 1.0f);
        world_bounds.min = glm::min(world_bounds.min, glm::vec3(tc));
        world_bounds.max = glm::max(world_bounds.max, glm::vec3(tc));
    }
    node.bounds = world_bounds;

    for (int child : node.children) {
        compute_node_transform(child, node.world_transform);
    }
}
