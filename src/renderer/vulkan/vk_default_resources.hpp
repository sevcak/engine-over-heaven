#pragma once

#include "vk_context.hpp"
#include "vk_descriptors.hpp"
#include "vk_material_system.hpp"
#include "vk_types.hpp"

/**
 * Fallback textures, samplers and a material, used wherever an asset doesn't provide its own (a
 * missing texture, a failed image load, a primitive without a material). Owned by `VulkanRenderer`,
 * read by the glTF loader.
 */
struct DefaultResources
{
    AllocatedImage white_image;
    AllocatedImage black_image;
    AllocatedImage grey_image;
    /** Magenta and black checkerboard, used in place of textures that failed to load. */
    AllocatedImage error_checkerboard_image;

    VkSampler sampler_linear;
    VkSampler sampler_nearest;

    /** Opaque glTF metallic-roughness material: white, non-metallic, half rough. */
    MaterialInstance default_material;
    /** Uniform buffer holding `default_material`'s constants. */
    AllocatedBuffer default_material_constants;

    /**
     * @brief Create the default images, samplers and material.
     *
     * @param gpu The GPU context to create the resources with.
     * @param material_system Material system with `GLTFMetallic_Roughness` registered and its
     * pipelines built.
     * @param descriptor_allocator Allocator the default material's descriptor set is allocated
     * from. Must outlive this.
     */
    void init(const GpuContext &gpu, MaterialSystem &material_system,
        DescriptorAllocatorGrowable &descriptor_allocator);

    /** Destroy everything `init` created. The GPU must be idle. */
    void destroy(const GpuContext &gpu);
};
