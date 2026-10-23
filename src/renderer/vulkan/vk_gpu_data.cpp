#include "vk_gpu_data.hpp"

void CullPushConstants::extract_frustum_planes(const glm::mat4 &vp)
{
    glm::vec4 row0 = glm::vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    glm::vec4 row1 = glm::vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    glm::vec4 row2 = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    glm::vec4 row3 = glm::vec4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

    frustum_planes[0] = row3 + row0; // Left.
    frustum_planes[1] = row3 - row0; // Right.
    frustum_planes[2] = row3 + row1; // Bottom.
    frustum_planes[3] = row3 - row1; // Top.
    frustum_planes[4] = row3 + row2; // Near.
    frustum_planes[5] = row3 - row2; // Far.

    // Normalize the planes.
    for (int i = 0; i < 6; i++) {
        f32 l = glm::length(glm::vec3(frustum_planes[i]));
        frustum_planes[i] /= l;
    }
}
