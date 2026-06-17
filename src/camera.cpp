#include "camera.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

Camera::Camera() {}

void Camera::set_target(glm::vec3 target) { target_ = target; }
void Camera::set_distance(float distance) { distance_ = distance; }
void Camera::set_aspect(float aspect) { aspect_ = aspect; }

void Camera::on_scroll(double yoffset) {
    scroll_accum_ += (float)yoffset;
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position(), target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projection() const {
    glm::mat4 p = glm::perspective(glm::radians(fov_), aspect_, near_, far_);
    p[1][1] *= -1.0f; // Vulkan NDC
    return p;
}

glm::vec3 Camera::position() const {
    float cos_pitch = cos(pitch_);
    return target_ + glm::vec3(
        distance_ * cos_pitch * sin(yaw_),
        distance_ * sin(pitch_),
        distance_ * cos_pitch * cos(yaw_)
    );
}

void Camera::update(float dt, GLFWwindow* window) {
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (!mouse_pressed_) {
            mouse_pressed_ = true;
            last_mouse_x_ = (float)mx;
            last_mouse_y_ = (float)my;
        }

        float dx = (float)mx - last_mouse_x_;
        float dy = (float)my - last_mouse_y_;

        yaw_ += dx * 0.003f;
        pitch_ += dy * 0.003f;
        pitch_ = glm::clamp(pitch_, -1.5f, 1.5f);

        last_mouse_x_ = (float)mx;
        last_mouse_y_ = (float)my;
    } else {
        mouse_pressed_ = false;
    }

    // Scroll zoom (from scroll callback)
    if (scroll_accum_ != 0.0f) {
        distance_ -= scroll_accum_ * 0.5f;
        scroll_accum_ = 0.0f;
    }

    // +/- keys zoom
    if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS)
        distance_ -= dt * 2.0f;
    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
        distance_ += dt * 2.0f;

    distance_ = glm::max(distance_, 0.05f);
}
