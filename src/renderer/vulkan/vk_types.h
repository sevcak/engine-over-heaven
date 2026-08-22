// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
#pragma once

#include <memory>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#define VK_CHECK(x)                                                                                \
    do {                                                                                           \
        VkResult err = x;                                                                          \
        if (err) {                                                                                 \
            fmt::println("Detected Vulkan error: {}", string_VkResult(err));                       \
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
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct Bounds
{
    glm::vec3 origin;
    float sphere_radius;
    glm::vec3 extents;
};

struct Vertex
{
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

/*
 * Structure holding the resources needed for a mesh.
 */
struct GPUMeshBuffers
{
    AllocatedBuffer index_buffer;
    AllocatedBuffer vertex_buffer;
    VkDeviceAddress vertex_buffer_address;
};

/*
 * Structure holding the push constants for mesh object drawing.
 */
struct GPUDrawPushConstants
{
    /* World/Model Matrix */
    glm::mat4 world_matrix;
    VkDeviceAddress vertex_buffer;
};

struct DrawContext;

class IRenderable
{
    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx) = 0;
};

struct Node : public IRenderable
{
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 local_transform;
    glm::mat4 world_transform;

    void refresh_transform(const glm::mat4 &parent_matrix)
    {
        world_transform = parent_matrix * local_transform;
        for (auto c : children) {
            c->refresh_transform(world_transform);
        }
    }

    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx)
    {
        for (auto &c : children) {
            c->draw(top_matrix, ctx);
        }
    }
};