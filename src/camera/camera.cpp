#include <camera/camera.hpp>

#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

glm::mat4 Camera::get_view_matrix() const
{
    glm::mat4 camera_translation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 camera_rotation = get_rotation_matrix();

    return glm::inverse(camera_translation * camera_rotation);
}

glm::mat4 Camera::get_rotation_matrix() const
{
    glm::quat pitch_rotation = glm::angleAxis(pitch, glm::vec3 { 1.0f, 0.0f, 0.0f });
    glm::quat yaw_rotation = glm::angleAxis(yaw, glm::vec3 { 0.0f, -1.0f, 0.0f });

    return glm::toMat4(yaw_rotation) * glm::toMat4(pitch_rotation);
}

glm::vec3 Camera::get_forward_vector() const
{
    glm::mat4 rotation = get_rotation_matrix();
    return glm::vec3(rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
}

glm::vec3 Camera::get_right_vector() const
{
    glm::mat4 rotation = get_rotation_matrix();
    return glm::vec3(rotation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
}

glm::vec3 Camera::get_up_vector() const
{
    glm::mat4 rotation = get_rotation_matrix();
    return glm::vec3(rotation * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
}