#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

class Camera {
public:
    Camera();

    void set_target(glm::vec3 target);
    void set_distance(float distance);
    void set_aspect(float aspect);

    void update(float delta_time, GLFWwindow* window);

    glm::mat4 view() const;
    glm::mat4 projection() const;
    glm::vec3 position() const;

    void on_scroll(double yoffset);

private:
    glm::vec3 target_ = glm::vec3(0.0f);
    float distance_ = 3.0f;
    float yaw_ = 0.0f;
    float pitch_ = -0.3f;
    float fov_ = 45.0f;
    float aspect_ = 16.0f / 9.0f;
    float near_ = 0.01f;
    float far_ = 1000.0f;

    float last_mouse_x_ = 0.0f;
    float last_mouse_y_ = 0.0f;
    bool mouse_pressed_ = false;
    float scroll_accum_ = 0.0f;
};
