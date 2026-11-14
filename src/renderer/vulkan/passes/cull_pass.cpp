#include "cull_pass.hpp"

#include <cvars.hpp>
#include <logger/logger.hpp>
#include <vk_gpu_data.hpp>
#include <vk_initializers.hpp>
#include <vk_pipelines.hpp>

#include <cstdlib>

static AutoCVar_Int cvar_cull(
    "r.cull", "Enable frustum culling (1 = on, 0 = off)", 1, CVarFlags::EditCheckbox);

void CullPass::init(
    const GpuContext &gpu, DescriptorAllocatorGrowable &allocator, std::span<FrameData> frames)
{
    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // Render Scene Object buffer.
    builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // The frame's indirect buffer.
    builder.add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // Render stats buffer.
    builder.add_binding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // Render Scene input commands buffer.
    builder.add_binding(
        4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // The frame's transparent order buffer.
    set_layout = builder.build(gpu.device, VK_SHADER_STAGE_COMPUTE_BIT);

    for (FrameData &frame : frames) {
        frame.compute_descriptor = allocator.allocate(gpu.device, set_layout);
    }

    VkShaderModule cull_shader;
    if (!vkutil::load_shader_module("../shaders/cull.comp.spv", gpu.device, &cull_shader)) {
        OH_LOG_ERROR("Error building the cull shader.");
        abort();
    }

    VkPipelineLayoutCreateInfo layout_info = vkinit::pipeline_layout_create_info();
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set_layout;

    VkPushConstantRange push_constant {};
    push_constant.offset = 0;
    push_constant.size = sizeof(CullPushConstants);
    push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    layout_info.pPushConstantRanges = &push_constant;
    layout_info.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(gpu.device, &layout_info, nullptr, &pipeline_layout));

    VkComputePipelineCreateInfo pipeline_info {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.stage =
        vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT, cull_shader);

    VK_CHECK(
        vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

    vkDestroyShaderModule(gpu.device, cull_shader, nullptr);
}

void CullPass::destroy(const GpuContext &gpu)
{
    vkDestroyPipeline(gpu.device, pipeline, nullptr);
    vkDestroyPipelineLayout(gpu.device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(gpu.device, set_layout, nullptr);
}

void CullPass::record(const GpuContext &gpu, VkCommandBuffer cmd, FrameData &frame,
    const RenderScene &scene, const glm::mat4 &viewproj)
{
    {
        DescriptorWriter writer;
        writer.write_buffer(
            0, scene.object_buffer.buffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.write_buffer(1, frame.indirect_buffer.buffer, VK_WHOLE_SIZE, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.write_buffer(2, frame.render_stats_buffer.buffer, sizeof(GPURenderStats), 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.write_buffer(3, scene.input_command_buffer.buffer, VK_WHOLE_SIZE, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.write_buffer(4, frame.transparent_order_buffer.buffer, VK_WHOLE_SIZE, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        writer.update_set(gpu.device, frame.compute_descriptor);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
        &frame.compute_descriptor, 0, nullptr);

    const u32 total_draws = static_cast<u32>(scene.index_buffers.size());

    CullPushConstants pc;
    pc.extract_frustum_planes(viewproj);
    pc.object_count = total_draws;
    pc.cull_enabled = cvar_cull.get();
    pc.first_transparent_index = scene.first_transparent_index;
    vkCmdPushConstants(
        cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPushConstants), &pc);

    vkCmdDispatch(cmd, (total_draws + 255) / 256, 1, 1);

    VkMemoryBarrier2 memory_barrier {};
    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memory_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    // The Command Fetcher needs to read the Indirect Buffer before drawing starts.
    memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

    VkDependencyInfo dep_info {};
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &memory_barrier;

    vkCmdPipelineBarrier2(cmd, &dep_info);
}
