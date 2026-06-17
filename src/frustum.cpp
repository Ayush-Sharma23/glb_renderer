#include "frustum.hpp"

void Frustum::extract(const glm::mat4& vp) {
    // Left
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    // Right
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    // Bottom
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    // Top
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    // Near
    planes[4] = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    // Far
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    for (auto& p : planes) {
        float len = glm::length(glm::vec3(p));
        p /= len;
    }
}

bool Frustum::test_sphere(const glm::vec3& center, float radius) const {
    for (auto& p : planes) {
        if (glm::dot(glm::vec3(p), center) + p.w + radius < 0.0f)
            return false;
    }
    return true;
}

bool Frustum::test_aabb(const glm::vec3& min, const glm::vec3& max) const {
    glm::vec3 center = (min + max) * 0.5f;
    glm::vec3 extents = (max - min) * 0.5f;
    for (auto& p : planes) {
        glm::vec3 n = glm::vec3(p);
        float r = extents.x * std::abs(n.x) + extents.y * std::abs(n.y) + extents.z * std::abs(n.z);
        if (glm::dot(n, center) + p.w + r < 0.0f)
            return false;
    }
    return true;
}
