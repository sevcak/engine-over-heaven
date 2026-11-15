#include "forward_pass.hpp"

#include <vk_initializers.hpp>

u32 record_forward_pass(VkCommandBuffer cmd, const RenderScene &scene, VkBuffer indirect_buffer,
    VkDescriptorSet scene_data_set, VkImageView color_view, VkImageView depth_view,
    VkExtent2D extent)
{
    const u32 total_draws = static_cast<u32>(scene.index_buffers.size());
    const u32 first_transparent = scene.first_transparent_index;

    VkRenderingAttachmentInfo color_attachment =
        vkinit::attachment_info(color_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depth_attachment =
        vkinit::depth_attachment_info(depth_view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo render_info =
        vkinit::rendering_info(extent, &color_attachment, &depth_attachment);
    vkCmdBeginRendering(cmd, &render_info);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = extent.width;
    viewport.height = extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = extent.width;
    scissor.extent.height = extent.height;

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    u32 drawcall_count = 0;

    // Track the batching state.
    const MaterialInstance *last_material = nullptr;
    VkBuffer last_index_buffer = VK_NULL_HANDLE;
    u32 batch_start = 0;
    u32 batch_count = 0;

    auto flush_batch = [&]() {
        if (batch_count == 0)
            return;

        // Bind the state for this batch.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, last_material->pipeline->pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            last_material->pipeline->layout, 0, 1, &scene_data_set, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            last_material->pipeline->layout, 1, 1, &last_material->material_set, 0, nullptr);

        vkCmdBindIndexBuffer(cmd, last_index_buffer, 0, VK_INDEX_TYPE_UINT32);

        u32 offset = batch_start * sizeof(VkDrawIndexedIndirectCommand);
        u32 stride = sizeof(VkDrawIndexedIndirectCommand);
        vkCmdDrawIndexedIndirect(cmd, indirect_buffer, offset, batch_count, stride);
        drawcall_count++;

        // Move the start index forward for the next batch.
        batch_start += batch_count;
        batch_count = 0;
    };

    for (u32 d = 0; d < total_draws; d++) {
        const u32 slot =
            d < first_transparent ? d : scene.transparent_render_order[d - first_transparent];
        const MaterialInstance *material = scene.materials[slot];
        VkBuffer index_buffer = scene.index_buffers[slot];

        // If the material or index buffer changes, flush the batch and start a new one.
        if (material != last_material || index_buffer != last_index_buffer) {
            flush_batch();
            last_material = material;
            last_index_buffer = index_buffer;
        }
        batch_count++;
    }
    flush_batch();

    vkCmdEndRendering(cmd);

    return drawcall_count;
}
