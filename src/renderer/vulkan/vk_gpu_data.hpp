#pragma once

/**
 * Structs whose memory layout the shaders also define. Keep them in sync with
 * `shaders/gpu_types.glsl`, `shaders/input_structures.glsl` and the buffers in `shaders/cull.comp`.
 */

#include <core/types.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct GPUObjectData
{
    glm::mat4 world_matrix;
    /**
     * Bounding sphere.
     *     - `.xyz` - Local origin.
     *     - `.w`   - Radius of the sphere.
     */
    glm::vec4 sphere_bounds;
    VkDeviceAddress vertex_buffer;
    u32 padding[2];
};

struct GPURenderStats
{
    u32 visible_triangles;
};

struct GPUSceneData
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::vec4 ambient_color;
    /**
     * `sunlight_direction`:
     *	- `.xyz`: Direction
     *  - `.w`: Sun power
     */
    glm::vec4 sunlight_direction;
    glm::vec4 sunlight_color;
    glm::vec4 camera_position;
};

struct CullPushConstants
{
    glm::vec4 frustum_planes[6];
    u32 object_count;
    u32 cull_enabled;
    u32 first_transparent_index;

    /**
     * Extract frustum planes from the view-projection matrix and store them into
     * `.frustum_planes`.
     *
     * Frustum planes are stored in the following order:
     *     - `0`: Left plane
     *     - `1`: Right plane
     *     - `2`: Bottom plane
     *     - `3`: Top plane
     *     - `4`: Near plane
     *     - `5`: Far plane
     *
     * @param vp View-projection matrix.
     */
    void extract_frustum_planes(const glm::mat4 &vp);
};
