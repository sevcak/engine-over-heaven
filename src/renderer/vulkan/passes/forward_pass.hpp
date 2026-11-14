#pragma once

#include "vk_render_scene.hpp"
#include "vk_types.hpp"

/**
 * @brief Record the forward pass: draw every visible object of the scene, fully lit, into the color
 * and depth attachments.
 *
 * Draws come from the indirect buffer the cull pass filled, in slot order: opaque first, then
 * transparent back to front. Consecutive draws that share a material and an index buffer are
 * merged into one `vkCmdDrawIndexedIndirect`.
 *
 * The color image must be in `COLOR_ATTACHMENT_OPTIMAL` and the depth image in
 * `DEPTH_ATTACHMENT_OPTIMAL` layout, and the cull pass's barrier must be recorded before this.
 *
 * @param cmd The command buffer to record into.
 * @param scene The scene to draw. Must contain at least one draw.
 * @param indirect_buffer The frame's indirect commands, written by the cull pass.
 * @param scene_data_set Set 0 of every material pipeline: scene data and the object buffer.
 * @param color_view The color attachment. Loaded, not cleared, since the background pass filled it.
 * @param depth_view The depth attachment, cleared at the start of the pass.
 * @param extent The region to render into, in pixels.
 * @return The number of indirect draw calls recorded.
 */
u32 record_forward_pass(VkCommandBuffer cmd, const RenderScene &scene, VkBuffer indirect_buffer,
    VkDescriptorSet scene_data_set, VkImageView color_view, VkImageView depth_view,
    VkExtent2D extent);
