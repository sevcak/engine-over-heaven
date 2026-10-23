#pragma once

#include <memory>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <core/types.hpp>

#include <fmt/core.h>
#include <logger/logger.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#define VK_CHECK(x)                                                                                \
    do {                                                                                           \
        VkResult err = x;                                                                          \
        if (err) {                                                                                 \
            OH_LOG_FATAL("Detected Vulkan error: {}", string_VkResult(err));                       \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

struct AllocatedImage
{
    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;
    VkExtent3D image_extent;
    VkFormat image_format;
};

struct AllocatedBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info {};
};

struct Bounds
{
    glm::vec3 origin;
    f32 sphere_radius;
    glm::vec3 extents;
};

struct Vertex
{
    glm::vec3 position;
    f32 uv_x;
    glm::vec3 normal;
    f32 uv_y;
    glm::vec4 color;
};

/**
 * Structure holding the resources needed for a mesh.
 */
struct GPUMeshBuffers
{
    AllocatedBuffer index_buffer;
    AllocatedBuffer vertex_buffer;
    VkDeviceAddress vertex_buffer_address;
};

struct DrawContext;

class IRenderable
{
    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx) = 0;
};
