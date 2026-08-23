#pragma once

#include "string_utils.hpp"
#include <string_view>
#include <unordered_map>
#include <vk_descriptors.hpp>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

class VulkanRenderer;

enum MaterialPass : uint8_t
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
    std::unordered_map<StringUtils::StringHash, std::unique_ptr<IMaterialType>> registered_types;

public:
    /**
     * Register a material type within the system.
     *
     * @param name The name of the material type.
     * @param type The material type to register.
     */
    void register_type(std::string_view name, std::unique_ptr<IMaterialType> type);

    /**
     * Get a material type by name.
     *
     * @param name The name of the material type.
     * @returns    A reference to the material type.
     */
    IMaterialType &get_type(std::string_view name);
};

class GLTFMetallic_Roughness : public IMaterialType
{
public:
    std::array<MaterialPipeline, static_cast<std::size_t>(MaterialPass::MaxPasses)> pipelines;

    VkDescriptorSetLayout material_layout;

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
        VkSampler metal_rough_smapler;
        VkBuffer data_buffer;
        uint32_t data_buffer_offset;
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