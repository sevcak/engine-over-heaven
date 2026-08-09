#pragma once

#include <cstdint>
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
    uint32_t padding[2];
};

struct GPURenderStats
{
    uint32_t visible_triangles;
};