#include "gltf_loader.hpp"
#include <cstdio>
#include <cstring>

#include "cgltf.h"

static glm::mat4 cgltf_to_glm(const cgltf_float* m) {
    glm::mat4 result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result[c][r] = m[r * 4 + c];
    return result;
}

static glm::vec3 cgltf_to_glm_vec3(const cgltf_float* v) {
    return glm::vec3(v[0], v[1], v[2]);
}

static glm::vec4 cgltf_to_glm_vec4(const cgltf_float* v) {
    return glm::vec4(v[0], v[1], v[2], v[3]);
}

static int find_texture_index(const cgltf_data* data, const cgltf_texture* tex) {
    if (!tex) return -1;
    return (int)(tex - data->textures);
}

static void build_node_transform(cgltf_node* node, glm::mat4& out) {
    if (node->has_matrix) {
        out = cgltf_to_glm(node->matrix);
    } else {
        glm::mat4 t = glm::translate(glm::mat4(1.0f),
            glm::vec3(node->translation[0], node->translation[1], node->translation[2]));
        glm::mat4 r = glm::mat4_cast(glm::quat(
            node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
        glm::mat4 s = glm::scale(glm::mat4(1.0f),
            glm::vec3(node->scale[0], node->scale[1], node->scale[2]));
        out = t * r * s;
    }
}

bool GltfLoader::load(const std::string& path) {
    cgltf_data* cgltf_data = nullptr;
    cgltf_options options = {};
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &cgltf_data);
    if (result != cgltf_result_success) {
        fprintf(stderr, "ERROR: Failed to parse '%s' (error code %d)\n", path.c_str(), result);
        return false;
    }

    result = cgltf_load_buffers(&options, cgltf_data, path.c_str());
    if (result != cgltf_result_success) {
        fprintf(stderr, "ERROR: Failed to load buffers for '%s'\n", path.c_str());
        cgltf_free(cgltf_data);
        return false;
    }

    // Load textures
    data_.textures.reserve(cgltf_data->textures_count);
    for (size_t i = 0; i < cgltf_data->textures_count; ++i) {
        cgltf_texture* tex = &cgltf_data->textures[i];
        GltfTexture gt;
        gt.name = tex->name ? tex->name : "unnamed";
        gt.uri = tex->image && tex->image->uri ? tex->image->uri : "";

        // Extract raw image bytes for embedded GLB textures
        if (tex->image && tex->image->buffer_view) {
            const cgltf_buffer_view* bv = tex->image->buffer_view;
            if (bv && bv->buffer && bv->buffer->data) {
                const uint8_t* src = (const uint8_t*)bv->buffer->data + bv->offset;
                gt.data.assign(src, src + bv->size);
            }
        }

        data_.textures.push_back(gt);
    }

    // Load materials
    data_.materials.reserve(cgltf_data->materials_count);
    for (size_t i = 0; i < cgltf_data->materials_count; ++i) {
        cgltf_material* mat = &cgltf_data->materials[i];
        GltfMaterial gm;
        gm.name = mat->name ? mat->name : "unnamed";
        gm.base_color_factor = cgltf_to_glm_vec4(mat->pbr_metallic_roughness.base_color_factor);
        gm.emissive_factor = cgltf_to_glm_vec3(mat->emissive_factor);
        gm.metallic_factor = mat->pbr_metallic_roughness.metallic_factor;
        gm.roughness_factor = mat->pbr_metallic_roughness.roughness_factor;
        gm.alpha_cutoff = mat->alpha_cutoff;
        gm.alpha_mode = (int)mat->alpha_mode;
        gm.double_sided = mat->double_sided;
        gm.base_color_texture = find_texture_index(cgltf_data, mat->pbr_metallic_roughness.base_color_texture.texture);
        gm.metallic_roughness_texture = find_texture_index(cgltf_data, mat->pbr_metallic_roughness.metallic_roughness_texture.texture);
        gm.normal_texture = find_texture_index(cgltf_data, mat->normal_texture.texture);
        gm.occlusion_texture = find_texture_index(cgltf_data, mat->occlusion_texture.texture);
        gm.emissive_texture = find_texture_index(cgltf_data, mat->emissive_texture.texture);
        data_.materials.push_back(gm);
    }

    // Load meshes
    data_.meshes.reserve(cgltf_data->meshes_count);
    for (size_t i = 0; i < cgltf_data->meshes_count; ++i) {
        cgltf_mesh* mesh = &cgltf_data->meshes[i];
        GltfMesh gm;
        gm.name = mesh->name ? mesh->name : "unnamed";
        gm.min_pos = glm::vec3(1e30f);
        gm.max_pos = glm::vec3(-1e30f);

        gm.primitives.reserve(mesh->primitives_count);
        for (size_t p = 0; p < mesh->primitives_count; ++p) {
            cgltf_primitive* prim = &mesh->primitives[p];
            GltfPrimitive gp;
            gp.material_index = -1;
            if (prim->material) {
                gp.material_index = (int)(prim->material - cgltf_data->materials);
            }

            // Count vertices
            int vertex_count = 0;
            for (size_t a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->data && (int)attr->data->count > vertex_count)
                    vertex_count = (int)attr->data->count;
            }
            gp.vertex_count = vertex_count;
            gp.vertices.resize(vertex_count);

            // Extract attributes
            for (size_t a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                cgltf_accessor* acc = attr->data;
                if (!acc || !acc->buffer_view) continue;

                cgltf_size stride = acc->stride ? acc->stride :
                    cgltf_num_components(acc->type) * cgltf_component_size(acc->component_type);
                const uint8_t* base = (const uint8_t*)acc->buffer_view->buffer->data
                    + acc->offset + acc->buffer_view->offset;

                for (int v = 0; v < vertex_count && v < (int)acc->count; ++v) {
                    const float* src = (const float*)(base + v * stride);
                    switch (attr->type) {
                        case cgltf_attribute_type_position:
                            gp.vertices[v].position = glm::vec3(src[0], src[1], src[2]);
                            gm.min_pos = glm::min(gm.min_pos, gp.vertices[v].position);
                            gm.max_pos = glm::max(gm.max_pos, gp.vertices[v].position);
                            break;
                        case cgltf_attribute_type_normal:
                            gp.vertices[v].normal = glm::vec3(src[0], src[1], src[2]);
                            break;
                        case cgltf_attribute_type_texcoord:
                            gp.vertices[v].uv = glm::vec2(src[0], src[1]);
                            break;
                        case cgltf_attribute_type_tangent:
                            gp.vertices[v].tangent = glm::vec4(src[0], src[1], src[2], src[3]);
                            break;
                        case cgltf_attribute_type_joints:
                        {
                            int ji[4] = {0};
                            if (acc->component_type == cgltf_component_type_r_8u ||
                                acc->component_type == cgltf_component_type_r_16u) {
                                for (int k = 0; k < 4 && k < (int)cgltf_num_components(acc->type); ++k) {
                                    ji[k] = (acc->component_type == cgltf_component_type_r_8u) ?
                                        ((const uint8_t*)src)[k] : ((const uint16_t*)src)[k];
                                }
                            }
                            gp.vertices[v].bone_indices = glm::uvec4(ji[0], ji[1], ji[2], ji[3]);
                            break;
                        }
                        case cgltf_attribute_type_weights:
                            gp.vertices[v].bone_weights = glm::vec4(src[0], src[1], src[2], src[3]);
                            break;
                        default:
                            break;
                    }
                }
            }

            // Extract indices
            cgltf_accessor* idx_acc = prim->indices;
            if (idx_acc && idx_acc->buffer_view) {
                gp.index_count = (int)idx_acc->count;
                gp.indices.resize(gp.index_count);
                cgltf_size idx_stride = idx_acc->stride ? idx_acc->stride :
                    cgltf_component_size(idx_acc->component_type);
                const uint8_t* idx_base = (const uint8_t*)idx_acc->buffer_view->buffer->data
                    + idx_acc->offset + idx_acc->buffer_view->offset;

                for (int i = 0; i < gp.index_count; ++i) {
                    switch (idx_acc->component_type) {
                        case cgltf_component_type_r_8u:
                            gp.indices[i] = ((const uint8_t*)idx_base)[i * idx_stride];
                            break;
                        case cgltf_component_type_r_16u:
                            gp.indices[i] = ((const uint16_t*)idx_base)[i * idx_stride / 2];
                            break;
                        case cgltf_component_type_r_32u:
                            gp.indices[i] = ((const uint32_t*)idx_base)[i * idx_stride / 4];
                            break;
                        default:
                            break;
                    }
                }
            } else {
                // Non-indexed geometry: generate sequential indices
                gp.index_count = vertex_count;
                gp.indices.resize(gp.index_count);
                for (int i = 0; i < gp.index_count; ++i)
                    gp.indices[i] = i;
            }

            gm.primitives.push_back(gp);
        }
        data_.meshes.push_back(gm);
    }

    // Load nodes
    data_.nodes.reserve(cgltf_data->nodes_count);
    for (size_t i = 0; i < cgltf_data->nodes_count; ++i) {
        cgltf_node* node = &cgltf_data->nodes[i];
        GltfNode gn;
        gn.name = node->name ? node->name : "unnamed";
        build_node_transform(node, gn.local_transform);
        gn.skin_index = -1;
        gn.parent_index = -1;
        if (node->skin) {
            gn.skin_index = (int)(node->skin - cgltf_data->skins);
        }
        if (node->parent) {
            gn.parent_index = (int)(node->parent - cgltf_data->nodes);
        }

        gn.children.reserve(node->children_count);
        for (size_t c = 0; c < node->children_count; ++c) {
            gn.children.push_back((int)(node->children[c] - cgltf_data->nodes));
        }

        gn.mesh_indices.reserve(node->mesh ? 1 : 0);
        if (node->mesh) {
            gn.mesh_indices.push_back((int)(node->mesh - cgltf_data->meshes));
        }

        data_.nodes.push_back(gn);
    }

    // Load skins
    data_.skins.reserve(cgltf_data->skins_count);
    for (size_t i = 0; i < cgltf_data->skins_count; ++i) {
        cgltf_skin* skin = &cgltf_data->skins[i];
        GltfSkin gs;
        gs.name = skin->name ? skin->name : "unnamed";
        gs.skeleton_root = skin->skeleton ? (int)(skin->skeleton - cgltf_data->nodes) : -1;
        gs.joint_nodes.reserve(skin->joints_count);
        for (size_t j = 0; j < skin->joints_count; ++j) {
            gs.joint_nodes.push_back((int)(skin->joints[j] - cgltf_data->nodes));
        }
        if (skin->inverse_bind_matrices) {
            cgltf_accessor* ibm = skin->inverse_bind_matrices;
            if (ibm->buffer_view) {
                cgltf_size stride = ibm->stride ? ibm->stride : sizeof(float) * 16;
                const uint8_t* base = (const uint8_t*)ibm->buffer_view->buffer->data
                    + ibm->offset + ibm->buffer_view->offset;
                for (size_t j = 0; j < ibm->count; ++j) {
                    gs.inverse_bind_matrices.push_back(cgltf_to_glm((const cgltf_float*)(base + j * stride)));
                }
            }
        }
        data_.skins.push_back(gs);
    }

    // Load animations
    data_.animations.reserve(cgltf_data->animations_count);
    for (size_t i = 0; i < cgltf_data->animations_count; ++i) {
        cgltf_animation* anim = &cgltf_data->animations[i];
        GltfAnimation ga;
        ga.name = anim->name ? anim->name : "unnamed";

        for (size_t c = 0; c < anim->channels_count; ++c) {
            cgltf_animation_channel* chan = &anim->channels[c];
            cgltf_animation_sampler* samp = chan->sampler;
            GltfAnimationChannel gac;
            gac.target_node = (int)(chan->target_node - cgltf_data->nodes);
            gac.target_path = std::string(chan->target_path == cgltf_animation_path_type_translation ? "translation" :
                chan->target_path == cgltf_animation_path_type_rotation ? "rotation" :
                chan->target_path == cgltf_animation_path_type_scale ? "scale" :
                chan->target_path == cgltf_animation_path_type_weights ? "weights" : "unknown");

            // Read times
            cgltf_accessor* time_acc = samp->input;
            if (time_acc && time_acc->buffer_view) {
                cgltf_size stride = time_acc->stride ? time_acc->stride : sizeof(float);
                const uint8_t* base = (const uint8_t*)time_acc->buffer_view->buffer->data
                    + time_acc->offset + time_acc->buffer_view->offset;
                gac.times.reserve(time_acc->count);
                for (size_t t = 0; t < time_acc->count; ++t)
                    gac.times.push_back(*(const float*)(base + t * stride));
            }

            // Read values
            cgltf_accessor* val_acc = samp->output;
            if (val_acc && val_acc->buffer_view) {
                cgltf_size num_comp = cgltf_num_components(val_acc->type);
                cgltf_size stride = val_acc->stride ? val_acc->stride : num_comp * sizeof(float);
                const uint8_t* base = (const uint8_t*)val_acc->buffer_view->buffer->data
                    + val_acc->offset + val_acc->buffer_view->offset;
                gac.values.reserve(val_acc->count);
                for (size_t t = 0; t < val_acc->count; ++t) {
                    const float* f = (const float*)(base + t * stride);
                    if (num_comp == 3)
                        gac.values.push_back(glm::vec4(f[0], f[1], f[2], 0.0f));
                    else if (num_comp == 4)
                        gac.values.push_back(glm::vec4(f[0], f[1], f[2], f[3]));
                    else
                        gac.values.push_back(glm::vec4(f[0], 0, 0, 0));
                }
            }

            ga.channels.push_back(gac);
        }
        data_.animations.push_back(ga);
    }

    // Load scenes
    data_.scenes.reserve(cgltf_data->scenes_count);
    for (size_t i = 0; i < cgltf_data->scenes_count; ++i) {
        cgltf_scene* scene = &cgltf_data->scenes[i];
        GltfScene gs;
        gs.name = scene->name ? scene->name : "unnamed";
        gs.root_nodes.reserve(scene->nodes_count);
        for (size_t n = 0; n < scene->nodes_count; ++n)
            gs.root_nodes.push_back((int)(scene->nodes[n] - cgltf_data->nodes));
        data_.scenes.push_back(gs);
    }

    data_.default_scene = cgltf_data->scene ? (int)(cgltf_data->scene - cgltf_data->scenes) : 0;

    cgltf_free(cgltf_data);
    return true;
}

void GltfLoader::cleanup() {
    data_ = GltfData{};
}

static void dump_indent(int depth) {
    for (int i = 0; i < depth; ++i) printf("  ");
}

void GltfData::dump() const {
    printf("========================================\n");
    printf("  GLTF Scene Dump\n");
    printf("========================================\n\n");

    printf("Scenes: %zu\n", scenes.size());
    for (size_t i = 0; i < scenes.size(); ++i) {
        printf("  [%zu] \"%s\" (roots: %zu)\n", i, scenes[i].name.c_str(), scenes[i].root_nodes.size());
    }
    printf("Default scene: %d\n\n", default_scene);

    printf("Nodes: %zu\n", nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto& n = nodes[i];
        dump_indent(1);
        printf("[%zu] \"%s\"", i, n.name.c_str());
        if (n.parent_index >= 0) printf(" parent=%d", n.parent_index);
        printf(" children=[");
        for (size_t c = 0; c < n.children.size(); ++c) {
            if (c > 0) printf(",");
            printf("%d", n.children[c]);
        }
        printf("] meshes=[");
        for (size_t m = 0; m < n.mesh_indices.size(); ++m) {
            if (m > 0) printf(",");
            printf("%d", n.mesh_indices[m]);
        }
        printf("]");
        if (n.skin_index >= 0) printf(" skin=%d", n.skin_index);
        printf("\n");
    }

    if (!animations.empty()) {
        printf("\nAnimations: %zu\n", animations.size());
        for (size_t i = 0; i < animations.size(); ++i) {
            auto& a = animations[i];
            printf("  [%zu] \"%s\" (%zu channels)\n", i, a.name.c_str(), a.channels.size());
            for (size_t c = 0; c < a.channels.size(); ++c) {
                auto& ch = a.channels[c];
                printf("    channel %zu: node=%d path=%s keys=%zu\n",
                    c, ch.target_node, ch.target_path.c_str(), ch.times.size());
            }
        }
    }

    if (!skins.empty()) {
        printf("\nSkins: %zu\n", skins.size());
        for (size_t i = 0; i < skins.size(); ++i) {
            auto& s = skins[i];
            printf("  [%zu] \"%s\" joints=%zu skeleton_root=%d\n",
                i, s.name.c_str(), s.joint_nodes.size(), s.skeleton_root);
        }
    }

    printf("\nMeshes: %zu\n", meshes.size());
    size_t total_vertices = 0, total_indices = 0;
    for (size_t i = 0; i < meshes.size(); ++i) {
        auto& m = meshes[i];
        printf("  [%zu] \"%s\"", i, m.name.c_str());
        printf(" primitives=%zu", m.primitives.size());
        printf(" bounds: (%f,%f,%f) - (%f,%f,%f)\n",
            m.min_pos.x, m.min_pos.y, m.min_pos.z,
            m.max_pos.x, m.max_pos.y, m.max_pos.z);
        for (size_t p = 0; p < m.primitives.size(); ++p) {
            auto& prim = m.primitives[p];
            printf("    primitive %zu: vertices=%d indices=%d material=%d",
                p, prim.vertex_count, prim.index_count, prim.material_index);
            printf("\n");
            total_vertices += prim.vertex_count;
            total_indices += prim.index_count;
        }
    }
    printf("  Total: %zu vertices, %zu indices\n\n", total_vertices, total_indices);

    printf("Materials: %zu\n", materials.size());
    for (size_t i = 0; i < materials.size(); ++i) {
        auto& m = materials[i];
        printf("  [%zu] \"%s\"\n", i, m.name.c_str());
        printf("    base_color: (%f,%f,%f,%f) metallic=%f roughness=%f\n",
            m.base_color_factor.x, m.base_color_factor.y, m.base_color_factor.z, m.base_color_factor.w,
            m.metallic_factor, m.roughness_factor);
        printf("    emissive: (%f,%f,%f) alpha_cutoff=%f double_sided=%d\n",
            m.emissive_factor.x, m.emissive_factor.y, m.emissive_factor.z,
            m.alpha_cutoff, m.double_sided);
        printf("    textures: base_color=%d metallic_roughness=%d normal=%d occlusion=%d emissive=%d\n",
            m.base_color_texture, m.metallic_roughness_texture,
            m.normal_texture, m.occlusion_texture, m.emissive_texture);
    }

    printf("\nTextures: %zu\n", textures.size());
    for (size_t i = 0; i < textures.size(); ++i) {
        auto& t = textures[i];
        printf("  [%zu] \"%s\" uri=\"%s\"\n", i, t.name.c_str(), t.uri.c_str());
    }

    printf("\n========================================\n");
    printf("  End of dump\n");
    printf("========================================\n");
}
