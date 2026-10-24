#pragma once

#include "vk_descriptors.hpp"
#include "vk_material_system.hpp"
#include "vk_types.hpp"
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

struct DefaultResources;
struct DrawContext;
struct GpuContext;

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

    /** Context the GPU resources were created with, used to destroy them. Must outlive this. */
    const GpuContext *gpu;

    LoadedGLTF() = default;
    LoadedGLTF(const LoadedGLTF &) = delete;
    LoadedGLTF &operator=(const LoadedGLTF &) = delete;

    ~LoadedGLTF() { clear_all(); }

    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx);

private:
    void clear_all();
};

/**
 * @brief Load a glTF or GLB file and upload its meshes, textures and materials to the GPU.
 *
 * @param gpu The GPU context to create the resources with. Must outlive the result.
 * @param defaults Fallback textures and samplers for anything the file doesn't provide.
 * @param material_system Material system to build the file's materials with.
 * @param file_path Path to the `.gltf` or `.glb` file.
 * @return The loaded file, or `nullptr` if it couldn't be parsed.
 */
std::unique_ptr<LoadedGLTF> load_gltf(const GpuContext &gpu, const DefaultResources &defaults,
    MaterialSystem &material_system, std::filesystem::path file_path);