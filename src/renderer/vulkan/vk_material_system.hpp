#pragma once

#include <unordered_map>
#include <vk_descriptors.hpp>
#include <typeindex>
#include <memory>
#include <concepts>
#include <cstdlib>
#include <core/types.hpp>
#include <logger/logger.hpp>

#include <vulkan/vulkan.h>

class VulkanRenderer;

enum MaterialPass : u8
{
    Opaque = 0,
    Transparent,
    Shadow,
    MaxPasses
};

struct MaterialPipeline
{
    VkPipeline pipeline { VK_NULL_HANDLE };
    VkPipelineLayout layout { VK_NULL_HANDLE };
};

struct MaterialInstance
{
    MaterialPipeline *pipeline;
    VkDescriptorSet material_set;
    MaterialPass pass_type;
};

class IMaterialType
{
public:
    virtual ~IMaterialType() = default;

    virtual void build_pipelines(VulkanRenderer *renderer) = 0;

    virtual void clear_resources(VkDevice device) = 0;
};

class MaterialSystem
{
private:
    std::unordered_map<std::type_index, std::unique_ptr<IMaterialType>> registered_types;

public:
    /**
     * Register a material type within the system.
     *
     * @tparam T The type of the material to register. Must be derived from \c IMaterialType.
     */
    template <std::derived_from<IMaterialType> T>
    void register_type()
    {
        std::type_index type_id = typeid(T);
        auto [_, inserted] = registered_types.try_emplace(type_id, std::make_unique<T>());
        if (!inserted) {
            OH_LOG_FATAL("Material type already registered: {}.", type_id.name());
            abort();
        }
    }

    /**
     * Get a material type.
     *
     * @tparam T The type of the material to retrieve. Must be derived from \c IMaterialType.
     */
    template <std::derived_from<IMaterialType> T>
    T &get_type() 
    {
        std::type_index type_id = typeid(T);
        auto it = registered_types.find(type_id);
        if (it == registered_types.end()) {
            OH_LOG_FATAL("Material type not registered: {}.", type_id.name());
            abort();
        }

        return *static_cast<T *>(it->second.get());
    }


    /**
     * Clear all resources associated with the material system.
     *
     * @param device The Vulkan device.
     */
    void clear_resources(VkDevice device);

    /**
     * Build all necessary Vulkan pipelines for the registered material types.
     *
     * @param renderer The Vulkan renderer instance used to create the pipelines.
     */
    void build_pipelines(VulkanRenderer *renderer);
};

class GLTFMetallic_Roughness : public IMaterialType
{
public:
    std::array<MaterialPipeline, static_cast<usize>(MaterialPass::MaxPasses)> pipelines;

    VkDescriptorSetLayout material_layout { VK_NULL_HANDLE };

    struct MaterialConstants
    {
        glm::vec4 color_factors;
        glm::vec4 metal_rough_factors;
        // Padding
        std::array<glm::vec4, 14> extra;
    };

    struct MaterialResources
    {
        AllocatedImage color_image;
        VkSampler color_sampler;
        AllocatedImage metal_rough_image;
        VkSampler metal_rough_sampler;
        VkBuffer data_buffer;
        u32 data_buffer_offset;
    };

    MaterialInstance write_material(VkDevice device, MaterialPass pass,
        const MaterialResources &resources, DescriptorAllocatorGrowable &descriptor_allocator);

    void build_pipelines(VulkanRenderer *renderer) override;

    void clear_resources(VkDevice device) override;
};

struct GLTFMaterial
{
    MaterialInstance data;
};