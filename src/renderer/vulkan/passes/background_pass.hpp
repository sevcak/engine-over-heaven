#pragma once

/**
 * @file
 * The background pass: a compute shader that fills the draw image before the geometry is drawn.
 * Owned by `VulkanRenderer`. The engine's debug UI picks the effect and edits its parameters.
 */

#include "vk_context.hpp"
#include "vk_descriptors.hpp"
#include "vk_types.hpp"

#include <glm/glm.hpp>
#include <vector>

/** Parameters for a background effect. Each shader interprets the four vectors its own way. */
struct ComputePushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

/** One selectable background shader with its current parameters. */
struct ComputeEffect
{
    const char *name;
    VkPipeline pipeline;
    ComputePushConstants data;
};

struct BackgroundPass
{
    std::vector<ComputeEffect> effects;
    /** Index into `effects` of the effect that `record` dispatches. */
    i32 current_effect_index { 0 };
    /** Shared by every effect: `set_layout` plus the `ComputePushConstants` range. */
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    /** Binding 0: the draw image as a compute storage image. */
    VkDescriptorSetLayout set_layout { VK_NULL_HANDLE };
    /** Points at the draw image in `GENERAL` layout. Freed with the allocator it came from. */
    VkDescriptorSet draw_image_set { VK_NULL_HANDLE };

    /**
     * @brief Create the descriptor set and the pipelines for every background effect.
     *
     * @param gpu The GPU context to create the resources with.
     * @param allocator Allocator `draw_image_set` is allocated from. Must outlive this pass.
     * @param draw_image_view The draw image to write into. Must stay valid as long as this pass,
     * since `draw_image_set` points at it.
     */
    void init(
        const GpuContext &gpu, DescriptorAllocatorGrowable &allocator, VkImageView draw_image_view);

    /** Destroy the pipelines and layouts. The GPU must be idle. */
    void destroy(const GpuContext &gpu);

    /**
     * @brief Record the dispatch of the current effect.
     *
     * The draw image must already be in `GENERAL` layout.
     *
     * @param cmd The command buffer to record into.
     * @param extent The region of the draw image to fill, in pixels.
     */
    void record(VkCommandBuffer cmd, VkExtent2D extent);
};
