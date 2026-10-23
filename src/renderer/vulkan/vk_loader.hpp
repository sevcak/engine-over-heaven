#pragma once

#include "vk_descriptors.hpp"
#include "vk_material_system.hpp"
#include "vk_types.hpp"
#include <fastgltf/types.hpp>
#include <filesystem>
#include <unordered_map>

struct GeoSurface
{
    u32 start_index;
    u32 count;
    Bounds bounds;
    std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset
{
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers mesh_buffers;
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

struct MeshNode : public Node
{
    std::shared_ptr<MeshAsset> mesh;

    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx) override;
};

class VulkanRenderer;
struct DrawContext;

struct LoadedGLTF : public IRenderable
{
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, AllocatedImage> images;
    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

    std::vector<std::shared_ptr<Node>> top_nodes;

    std::vector<VkSampler> samplers;

    DescriptorAllocatorGrowable descriptor_pool;

    AllocatedBuffer material_data_buffer;

    // TODO: This should be replaced with a proper resource management system.
    VulkanRenderer *creator;

    LoadedGLTF() = default;
    LoadedGLTF(const LoadedGLTF &) = delete;
    LoadedGLTF &operator=(const LoadedGLTF &) = delete;

    ~LoadedGLTF() { clear_all(); }

    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx);

private:
    void clear_all();
};

std::unique_ptr<LoadedGLTF> load_gltf(VulkanRenderer *renderer, std::filesystem::path file_path);