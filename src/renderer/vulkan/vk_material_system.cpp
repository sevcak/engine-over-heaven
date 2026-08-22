#include "vk_material_system.hpp"
#include "vk_initializers.h"
#include "vk_pipelines.h"
#include "vk_renderer.hpp"
#include <memory>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

void MaterialSystem::register_type(std::string_view name, std::unique_ptr<IMaterialType> type)
{
    // TODO: Add a fatal log message.
    assert(!registered_types.contains(name));

    registered_types[std::move(name)] = std::move(type);
}

IMaterialType &MaterialSystem::get_type(std::string_view name)
{
    auto it = registered_types.find(name);
    // TODO: Add a fatal log message.
    assert(it != registered_types.end());
    return *it->second;
}

void GLTFMetallic_Roughness::build_pipelines(VulkanRenderer *renderer)
{
    VkShaderModule mesh_frag_shader;
    if (!vkutil::load_shader_module(
            "../shaders/mesh.frag.spv", renderer->_device, &mesh_frag_shader)) {
        fmt::println("Error building the triangle fragment shader module.");
    }

    VkShaderModule mesh_vertex_shader;
    if (!vkutil::load_shader_module(
            "../shaders/mesh.vert.spv", renderer->_device, &mesh_vertex_shader)) {
        fmt::println("Error building the traingle vertex shader module.");
    }

    VkPushConstantRange matrix_range {};
    matrix_range.offset = 0;
    matrix_range.size = sizeof(GPUDrawPushConstants);
    matrix_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layout_builder;
    layout_builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layout_builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layout_builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    material_layout = layout_builder.build(
        renderer->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = { renderer->_gpu_scene_data_descriptor_layout,
        material_layout };

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = &matrix_range;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout new_layout;
    VK_CHECK(vkCreatePipelineLayout(renderer->_device, &mesh_layout_info, nullptr, &new_layout));

    auto &opaque_pipeline = pipelines[MaterialPass::Opaque];
    auto &transparent_pipeline = pipelines[MaterialPass::Transparent];

    opaque_pipeline.layout = new_layout;
    transparent_pipeline.layout = new_layout;

    PipelineBuilder pipeline_builder;
    pipeline_builder.set_shaders(mesh_vertex_shader, mesh_frag_shader);
    pipeline_builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipeline_builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipeline_builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipeline_builder.set_multisampling_none();
    pipeline_builder.disable_blending();
    pipeline_builder.enable_depth_test(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipeline_builder.set_color_attachment_format(renderer->_draw_image.image_format);
    pipeline_builder.set_depth_format(renderer->_depth_image.image_format);
    pipeline_builder._pipeline_layout = new_layout;

    opaque_pipeline.pipeline = pipeline_builder.build_pipeline(renderer->_device);

    pipeline_builder.enable_blending_additive();
    pipeline_builder.enable_depth_test(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparent_pipeline.pipeline = pipeline_builder.build_pipeline(renderer->_device);

    vkDestroyShaderModule(renderer->_device, mesh_frag_shader, nullptr);
    vkDestroyShaderModule(renderer->_device, mesh_vertex_shader, nullptr);

    renderer->_main_deletion_queue.push_function([=, this, device = renderer->_device]() {
        vkDestroyDescriptorSetLayout(device, material_layout, nullptr);
        vkDestroyPipelineLayout(device, opaque_pipeline.layout, nullptr);
        vkDestroyPipeline(device, opaque_pipeline.pipeline, nullptr);
        vkDestroyPipeline(device, transparent_pipeline.pipeline, nullptr);
    });
}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass,
    const MaterialResources &resources, DescriptorAllocatorGrowable &descriptor_allocator)
{
    MaterialInstance mat_data;
    mat_data.pass_type = pass;
    if (pass == MaterialPass::Transparent) {
        mat_data.pipeline = &pipelines[MaterialPass::Transparent];
    } else {
        mat_data.pipeline = &pipelines[MaterialPass::Opaque];
    }

    mat_data.material_set = descriptor_allocator.allocate(device, material_layout);

    DescriptorWriter writer;
    writer.clear();
    writer.write_buffer(0, resources.data_buffer, sizeof(MaterialConstants),
        resources.data_buffer_offset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.color_image.image_view, resources.color_sampler,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metal_rough_image.image_view, resources.metal_rough_smapler,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(device, mat_data.material_set);

    return mat_data;
}

void GLTFMetallic_Roughness::clear_resources(VkDevice device)
{
    vkDestroyDescriptorSetLayout(device, material_layout, nullptr);

    vkDestroyPipelineLayout(device, pipelines[MaterialPass::Opaque].layout, nullptr);

    vkDestroyPipeline(device, pipelines[MaterialPass::Opaque].pipeline, nullptr);
    vkDestroyPipeline(device, pipelines[MaterialPass::Transparent].pipeline, nullptr);
}