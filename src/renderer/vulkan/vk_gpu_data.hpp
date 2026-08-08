#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct alignas(16) GPUObjectData
{
    glm::mat4 world_matrix;
    VkDeviceAddress vertex_buffer;
    uint32_t padding_0;
    uint32_t padding_1;
};