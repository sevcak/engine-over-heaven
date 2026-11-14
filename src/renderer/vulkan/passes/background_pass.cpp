#include "background_pass.hpp"

#include <logger/logger.hpp>
#include <vk_pipelines.hpp>

#include <cmath>

void BackgroundPass::init(
    const GpuContext &gpu, DescriptorAllocatorGrowable &allocator, VkImageView draw_image_view)
{
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        set_layout = builder.build(gpu.device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    draw_image_set = allocator.allocate(gpu.device, set_layout);

    DescriptorWriter writer;
    writer.write_image(
        0, draw_image_view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(gpu.device, draw_image_set);

    VkPipelineLayoutCreateInfo compute_layout = {};
    compute_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compute_layout.pNext = nullptr;
    compute_layout.pSetLayouts = &set_layout;
    compute_layout.setLayoutCount = 1;

    VkPushConstantRange push_constant {};
    push_constant.offset = 0;
    push_constant.size = sizeof(ComputePushConstants);
    push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    compute_layout.pPushConstantRanges = &push_constant;
    compute_layout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(gpu.device, &compute_layout, nullptr, &pipeline_layout));

    VkShaderModule gradient_shader;
    if (!vkutil::load_shader_module(
            "../shaders/gradient_color.comp.spv", gpu.device, &gradient_shader)) {
        OH_LOG_ERROR("Error building the gradient compute shader.");
    }
    VkShaderModule sky_shader;
    if (!vkutil::load_shader_module("../shaders/sky.comp.spv", gpu.device, &sky_shader)) {
        OH_LOG_ERROR("Error building the sky compute shader.");
    }

    VkPipelineShaderStageCreateInfo stage_info = {};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.pNext = nullptr;
    stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_info.module = gradient_shader;
    stage_info.pName = "main";

    VkComputePipelineCreateInfo compute_pipeline_create_info = {};
    compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute_pipeline_create_info.pNext = nullptr;
    compute_pipeline_create_info.layout = pipeline_layout;
    compute_pipeline_create_info.stage = stage_info;

    ComputeEffect gradient_effect;
    gradient_effect.name = "Gradient";
    gradient_effect.data = {};
    gradient_effect.data.data1 = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    gradient_effect.data.data2 = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    VK_CHECK(vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info,
        nullptr, &gradient_effect.pipeline));

    effects.push_back(gradient_effect);

    compute_pipeline_create_info.stage.module = sky_shader;

    ComputeEffect sky_effect;
    sky_effect.name = "Sky";
    sky_effect.data = {};
    sky_effect.data.data1 = glm::vec4(0.1f, 0.2f, 0.4f, 0.97f);

    VK_CHECK(vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info,
        nullptr, &sky_effect.pipeline));

    effects.push_back(sky_effect);

    vkDestroyShaderModule(gpu.device, gradient_shader, nullptr);
    vkDestroyShaderModule(gpu.device, sky_shader, nullptr);
}

void BackgroundPass::destroy(const GpuContext &gpu)
{
    for (const ComputeEffect &effect : effects) {
        vkDestroyPipeline(gpu.device, effect.pipeline, nullptr);
    }
    effects.clear();

    vkDestroyPipelineLayout(gpu.device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(gpu.device, set_layout, nullptr);
}

void BackgroundPass::record(VkCommandBuffer cmd, VkExtent2D extent)
{
    const ComputeEffect &effect = effects[current_effect_index];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
        &draw_image_set, 0, nullptr);

    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
        sizeof(ComputePushConstants), &effect.data);

    vkCmdDispatch(cmd, std::ceil(extent.width / 16.0f), std::ceil(extent.height / 16.0f), 1);
}
