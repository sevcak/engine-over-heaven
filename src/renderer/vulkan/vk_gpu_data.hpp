#pragma once

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