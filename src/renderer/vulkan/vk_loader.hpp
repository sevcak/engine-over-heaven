#pragma once

#include "vk_descriptors.h"
#include "vk_material_system.hpp"
#include "vk_types.h"
#include <fastgltf/types.hpp>
#include <filesystem>
#include <unordered_map>

struct GeoSurface
{
    uint32_t start_index;
    uint32_t count;
    Bounds bounds;
    std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset
{
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers mesh_buffers;
};

struct MeshNode : public Node
{
    std::shared_ptr<MeshAsset> mesh;

    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx) override;
};

class VulkanRenderer;
struct DrawContext;

std::optional<std::vector<std::shared_ptr<MeshAsset>>> load_gltf_meshes(
    VulkanRenderer *renderer, std::filesystem::path file_path);

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

    VulkanRenderer *creator;

    ~LoadedGLTF() { clear_all(); }

    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx);

private:
    void clear_all();
};

std::optional<std::shared_ptr<LoadedGLTF>> load_gltf(
    VulkanRenderer *renderer, std::string_view file_path);