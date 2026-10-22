#pragma once

#include <core/types.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

class Camera {
public:
    glm::vec3 velocity { 0.0f };
    glm::vec3 position { 0.0f };
    f32 pitch { 0.0f };
    f32 yaw { 0.0f };

    glm::mat4 get_view_matrix() const;
    glm::mat4 get_rotation_matrix() const;

    glm::vec3 get_forward_vector() const;
    glm::vec3 get_right_vector() const;
    glm::vec3 get_up_vector() const;
};
