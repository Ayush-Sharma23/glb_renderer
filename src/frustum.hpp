#pragma once

#include <glm/glm.hpp>

struct Frustum {
    glm::vec4 planes[6]; // left, right, bottom, top, near, far

    void extract(const glm::mat4& vp);

    bool test_sphere(const glm::vec3& center, float radius) const;
    bool test_aabb(const glm::vec3& min, const glm::vec3& max) const;
};
