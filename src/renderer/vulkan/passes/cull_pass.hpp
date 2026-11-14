#pragma once

#include "vk_context.hpp"
#include "vk_descriptors.hpp"
#include "vk_frame.hpp"
#include "vk_render_scene.hpp"

#include <glm/glm.hpp>
#include <span>

/**
 * The GPU frustum-cull pass: a compute shader that tests every object in a `RenderScene` against
 * the camera frustum and writes the visible ones into the frame's indirect buffer, which the
 * geometry pass then draws from. Owned by `VulkanRenderer`.
 */
struct CullPass
{
    VkPipeline pipeline { VK_NULL_HANDLE };
    /** `set_layout` plus the `CullPushConstants` range. */
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    /**
     * Five storage buffers: scene objects, the frame's indirect commands (output), the frame's
     * render stats, the scene's input commands, and the frame's transparent order.
     */
    VkDescriptorSetLayout set_layout { VK_NULL_HANDLE };

    /**
     * @brief Create the pipeline and allocate each frame's `compute_descriptor`.
     *
     * @param gpu The GPU context to create the resources with.
     * @param allocator Allocator the per-frame sets are allocated from. Must outlive this pass.
     * @param frames The frames in flight, whose `compute_descriptor` this fills in.
     */
    void init(
        const GpuContext &gpu, DescriptorAllocatorGrowable &allocator, std::span<FrameData> frames);

    /** Destroy the pipeline and layouts. The GPU must be idle. */
    void destroy(const GpuContext &gpu);

    /**
     * @brief Record the cull dispatch and the barrier that makes its output visible to indirect
     * draws.
     *
     * Points the frame's `compute_descriptor` at the scene's and the frame's buffers first, so the
     * GPU must be done with this frame. The frame's transparent order buffer must already hold
     * this frame's back-to-front order.
     *
     * @param gpu The GPU context, used for the descriptor write.
     * @param cmd The command buffer to record into.
     * @param frame The frame being recorded. Its indirect buffer receives the visible draws.
     * @param scene The scene to cull. Must contain at least one draw.
     * @param viewproj The camera's view-projection matrix, from which the frustum planes are taken.
     */
    void record(const GpuContext &gpu, VkCommandBuffer cmd, FrameData &frame,
        const RenderScene &scene, const glm::mat4 &viewproj);
};
