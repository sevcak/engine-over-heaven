#include "vk_renderer.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <cstdlib>
#include <vk_gpu_data.hpp>
#include <vk_images.h>
#include <vk_initializers.h>
#include <vk_loader.h>
#include <vk_pipelines.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include <glm/packing.hpp>

#include "cvars.hpp"

static AutoCVar_Float cvar_render_scale(
    "r.render_scale", "Resolution scale for rendering", 1.0f, CVarFlags::EditFloatDrag);
static AutoCVar_Int cvar_cull(
    "r.cull", "Enable frustum culling (1 = on, 0 = off)", 1, CVarFlags::EditCheckbox);

constexpr bool use_validation_layers = true;

bool is_visible(RenderObject &obj, const glm::mat4 &viewproj)
{
    if (cvar_cull.get() == 0) {
        return true;
    }

    std::array<glm::vec3, 8> corners {
        glm::vec3 { 1.f, 1.f, 1.f },
        glm::vec3 { 1.f, 1.f, -1.f },
        glm::vec3 { 1.f, -1.f, 1.f },
        glm::vec3 { 1.f, -1.f, -1.f },
        glm::vec3 { -1.f, 1.f, 1.f },
        glm::vec3 { -1.f, 1.f, -1.f },
        glm::vec3 { -1.f, -1.f, 1.f },
        glm::vec3 { -1.f, -1.f, -1.f },
    };

    glm::mat4 matrix = viewproj * obj.transform;

    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
        // Project corner into clip space.
        glm::vec4 v =
            matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.0f);

        // Perspective correction.
        v.x /= v.w;
        v.y /= v.w;
        v.z /= v.w;

        min = glm::min(glm::vec3 { v }, min);
        max = glm::max(glm::vec3 { v }, max);
    }

    // Check the clip space box is within view.
    if (min.z > 1.0f || max.z < 0.0f || min.x > 1.0f || max.x < -1.0f || min.y > 1.0f ||
        max.y < -1.f) {
        return false;
    }
    return true;
}

void EngineStats::reset_counters()
{
    drawcall_count = 0;
    triangle_count = 0;
}

void VulkanRenderer::init(SDL_Window *window, uint32_t width, uint32_t height)
{
    _window = window;
    _window_extent = { width, height };

    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync_structures();
    init_descriptors();
    init_pipelines();
    init_default_data();
    init_object_buffers();
    init_imgui();

    _is_initialized = true;
}

void VulkanRenderer::cleanup()
{
    if (_is_initialized) {
        vkDeviceWaitIdle(_device);

        for (int i = 0; i < FRAME_OVERLAP; i++) {
            vkDestroyCommandPool(_device, _frames[i].command_pool, nullptr);

            vkDestroyFence(_device, _frames[i].render_fence, nullptr);
            vkDestroySemaphore(_device, _frames[i].swapchain_semaphore, nullptr);

            _frames[i].deletion_queue.flush();
        }
        for (size_t i = 0; i < _render_semaphores.size(); i++) {
            vkDestroySemaphore(_device, _render_semaphores[i], nullptr);
        }

        _main_deletion_queue.flush();

        destroy_swapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);

        vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
        vkDestroyInstance(_instance, nullptr);
    }

    _is_initialized = false;
}

void VulkanRenderer::draw(
    DrawContext &main_draw_context, Camera &main_camera, EngineStats &stats, bool &resize_requested)
{
    // Wait until the GPU has finished rendering the last frame.
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame().render_fence, true, 1000000000));

    get_current_frame().deletion_queue.flush();
    get_current_frame().frame_descriptors.clear_pools(_device);

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame().render_fence));

    uint32_t swapchain_image_index;
    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000,
        get_current_frame().swapchain_semaphore, nullptr, &swapchain_image_index);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
        return;
    } else if (e != VK_SUCCESS && e != VK_SUBOPTIMAL_KHR) {
        fmt::println("Failed to acquire swapchain image: {}", string_VkResult(e));
        abort();
    }

    VkCommandBuffer cmd = get_current_frame().main_command_buffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmd_begin_info =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

    float render_scale = cvar_render_scale.get();
    uint32_t base_width = std::min(_swapchain_extent.width, _draw_image.image_extent.width);
    uint32_t base_height = std::min(_swapchain_extent.height, _draw_image.image_extent.height);
    _draw_extent.width = std::max(1u, static_cast<uint32_t>(base_width * render_scale));
    _draw_extent.height = std::max(1u, static_cast<uint32_t>(base_height * render_scale));

    // Transition the main draw image into general layout so we can compute-write into it.
    vkutil::transition_image(
        cmd, _draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    draw_background(cmd);

    // Transition the draw and depth images into correct layouts for the graphics pipeline.
    vkutil::transition_image(
        cmd, _draw_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _depth_image.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    draw_geometry(cmd, main_draw_context, main_camera, stats);

    // Transition the draw image and the swapchain image into their correct transfer layouts.
    vkutil::transition_image(cmd, _draw_image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, _swapchain_images[swapchain_image_index],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy from the draw image into the swapchain.
    vkutil::copy_image_to_image(cmd, _draw_image.image, _swapchain_images[swapchain_image_index],
        _draw_extent, _swapchain_extent);

    // Transition the swapchain image so we can draw into it.
    vkutil::transition_image(cmd, _swapchain_images[swapchain_image_index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Draw ImGui into the swapchain image.
    draw_imgui(cmd, _swapchain_image_views[swapchain_image_index], stats);

    // Set swapchain image layout to Present so it can be shown on the screen.
    vkutil::transition_image(cmd, _swapchain_images[swapchain_image_index],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmd_info = vkinit::command_buffer_submit_info(cmd);

    VkSemaphoreSubmitInfo wait_info =
        vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            get_current_frame().swapchain_semaphore);
    VkSemaphore signal_semaphore = _render_semaphores[swapchain_image_index];
    VkSemaphoreSubmitInfo signal_info =
        vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, signal_semaphore);

    VkSubmitInfo2 submit_info = vkinit::submit_info(&cmd_info, &signal_info, &wait_info);

    VK_CHECK(vkQueueSubmit2(_graphics_queue, 1, &submit_info, get_current_frame().render_fence));

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.pSwapchains = &_swapchain;
    present_info.swapchainCount = 1;

    present_info.pWaitSemaphores = &signal_semaphore;
    present_info.waitSemaphoreCount = 1;
    present_info.pImageIndices = &swapchain_image_index;

    VkResult present_result = vkQueuePresentKHR(_graphics_queue, &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
        return;
    } else if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
        fmt::println("Failed to present swapchain image: {}", string_VkResult(present_result));
        abort();
    }

    _frame_number++;
}

void VulkanRenderer::init_vulkan()
{
    vkb::InstanceBuilder builder;

    auto inst_ret = builder.set_app_name("Vulkan Application")
                        .request_validation_layers(use_validation_layers)
                        .use_default_debug_messenger()
                        .require_api_version(1, 3, 0)
                        .build();

    vkb::Instance vkb_inst = inst_ret.value();

    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    VkPhysicalDeviceFeatures features = { .multiDrawIndirect = true };

    VkPhysicalDeviceVulkan13Features features13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    vkb::PhysicalDeviceSelector selector { vkb_inst };
    vkb::PhysicalDevice physical_device = selector.set_minimum_version(1, 3)
                                              .set_required_features(features)
                                              .set_required_features_13(features13)
                                              .set_required_features_12(features12)
                                              .set_surface(_surface)
                                              .select()
                                              .value();

    vkb::DeviceBuilder device_builder { physical_device };

    vkb::Device vkb_device = device_builder.build().value();

    _device = vkb_device.device;
    _chosen_gpu = physical_device.physical_device;

    _graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    _graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.physicalDevice = _chosen_gpu;
    allocator_info.device = _device;
    allocator_info.instance = _instance;
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocator_info, &_allocator);
    _main_deletion_queue.push_function([&]() { vmaDestroyAllocator(_allocator); });
}

void VulkanRenderer::init_swapchain()
{
    create_swapchain(_window_extent.width, _window_extent.height);

    VkExtent3D draw_image_extent = { _window_extent.width, _window_extent.height, 1 };

    _draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    _draw_image.image_extent = draw_image_extent;

    VkImageUsageFlags draw_image_usages {};
    draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_STORAGE_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info =
        vkinit::image_create_info(_draw_image.image_format, draw_image_usages, draw_image_extent);

    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_draw_image.image,
        &_draw_image.allocation, nullptr);

    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(
        _draw_image.image_format, _draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_draw_image.image_view));

    _depth_image.image_format = VK_FORMAT_D32_SFLOAT;
    _depth_image.image_extent = draw_image_extent;

    VkImageUsageFlags depth_image_usages {};
    depth_image_usages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info =
        vkinit::image_create_info(_depth_image.image_format, depth_image_usages, draw_image_extent);

    vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depth_image.image,
        &_depth_image.allocation, nullptr);

    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(
        _depth_image.image_format, _depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depth_image.image_view));

    _main_deletion_queue.push_function([=, this]() {
        vkDestroyImageView(_device, _draw_image.image_view, nullptr);
        vmaDestroyImage(_allocator, _draw_image.image, _draw_image.allocation);

        vkDestroyImageView(_device, _depth_image.image_view, nullptr);
        vmaDestroyImage(_allocator, _depth_image.image, _depth_image.allocation);
    });
}

void VulkanRenderer::init_commands()
{
    VkCommandPoolCreateInfo command_pool_info = vkinit::command_pool_create_info(
        _graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(
            vkCreateCommandPool(_device, &command_pool_info, nullptr, &_frames[i].command_pool));

        VkCommandBufferAllocateInfo cmd_alloc_info =
            vkinit::command_buffer_allocate_info(_frames[i].command_pool, 1);

        VK_CHECK(
            vkAllocateCommandBuffers(_device, &cmd_alloc_info, &_frames[i].main_command_buffer));
    }

    VK_CHECK(vkCreateCommandPool(_device, &command_pool_info, nullptr, &_imm_cmd_pool));
    VkCommandBufferAllocateInfo cmd_alloc_info =
        vkinit::command_buffer_allocate_info(_imm_cmd_pool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmd_alloc_info, &_imm_cmd_buffer));

    _main_deletion_queue.push_function(
        [=, this]() { vkDestroyCommandPool(_device, _imm_cmd_pool, nullptr); });
}

void VulkanRenderer::init_sync_structures()
{
    VkFenceCreateInfo fence_create_info = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphore_create_info = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(_device, &fence_create_info, nullptr, &_frames[i].render_fence));
        VK_CHECK(vkCreateSemaphore(
            _device, &semaphore_create_info, nullptr, &_frames[i].swapchain_semaphore));
    }

    int swapchain_image_count = _swapchain_images.size();
    _render_semaphores.resize(swapchain_image_count);
    for (int i = 0; i < swapchain_image_count; i++) {
        VK_CHECK(
            vkCreateSemaphore(_device, &semaphore_create_info, nullptr, &_render_semaphores[i]));
    }

    VK_CHECK(vkCreateFence(_device, &fence_create_info, nullptr, &_imm_fence));
    _main_deletion_queue.push_function(
        [=, this]() { vkDestroyFence(_device, _imm_fence, nullptr); });
}

void VulkanRenderer::init_descriptors()
{
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
    };

    _global_descriptor_allocator.init(_device, 10, sizes);

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _draw_image_descriptor_layout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    _draw_image_descriptors =
        _global_descriptor_allocator.allocate(_device, _draw_image_descriptor_layout);

    DescriptorWriter writer;
    writer.write_image(0, _draw_image.image_view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(_device, _draw_image_descriptors);

    _main_deletion_queue.push_function([&]() {
        _global_descriptor_allocator.destroy_pools(_device);

        vkDestroyDescriptorSetLayout(_device, _draw_image_descriptor_layout, nullptr);
    });

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 }
        };

        _frames[i].frame_descriptors = DescriptorAllocatorGrowable {};
        _frames[i].frame_descriptors.init(_device, 1000, frame_sizes);

        _main_deletion_queue.push_function(
            [&, i]() { _frames[i].frame_descriptors.destroy_pools(_device); });
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        _gpu_scene_data_descriptor_layout =
            builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    _main_deletion_queue.push_function([&]() {
        vkDestroyDescriptorSetLayout(_device, _gpu_scene_data_descriptor_layout, nullptr);
    });

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _single_image_descriptor_layout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    _main_deletion_queue.push_function(
        [&]() { vkDestroyDescriptorSetLayout(_device, _single_image_descriptor_layout, nullptr); });
}

void VulkanRenderer::init_pipelines()
{
    init_background_pipelines();
    init_mesh_pipeline();
    _metal_rough_material.build_pipelines(this);
}

void VulkanRenderer::init_background_pipelines()
{
    VkPipelineLayoutCreateInfo compute_layout = {};
    compute_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compute_layout.pNext = nullptr;
    compute_layout.pSetLayouts = &_draw_image_descriptor_layout;
    compute_layout.setLayoutCount = 1;

    VkPushConstantRange push_constant {};
    push_constant.offset = 0;
    push_constant.size = sizeof(ComputePushConstants);
    push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    compute_layout.pPushConstantRanges = &push_constant;
    compute_layout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &compute_layout, nullptr, &_gradient_pipeline_layout));

    VkShaderModule gradient_shader;
    if (!vkutil::load_shader_module(
            "../shaders/gradient_color.comp.spv", _device, &gradient_shader)) {
        fmt::print("Error building the gradient compute shader.\n");
    }
    VkShaderModule sky_shader;
    if (!vkutil::load_shader_module("../shaders/sky.comp.spv", _device, &sky_shader)) {
        fmt::print("Error building the sky compute shader.\n");
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
    compute_pipeline_create_info.layout = _gradient_pipeline_layout;
    compute_pipeline_create_info.stage = stage_info;

    ComputeEffect gradient_effect;
    gradient_effect.layout = _gradient_pipeline_layout;
    gradient_effect.name = "Gradient";
    gradient_effect.data = {};
    gradient_effect.data.data1 = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    gradient_effect.data.data2 = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info,
        nullptr, &gradient_effect.pipeline));

    _background_effects.push_back(gradient_effect);

    compute_pipeline_create_info.stage.module = sky_shader;

    ComputeEffect sky_effect;
    sky_effect.layout = _gradient_pipeline_layout;
    sky_effect.name = "Sky";
    sky_effect.data = {};
    sky_effect.data.data1 = glm::vec4(0.1f, 0.2f, 0.4f, 0.97f);

    VK_CHECK(vkCreateComputePipelines(
        _device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info, nullptr, &sky_effect.pipeline));

    _background_effects.push_back(sky_effect);

    vkDestroyShaderModule(_device, gradient_shader, nullptr);
    vkDestroyShaderModule(_device, sky_shader, nullptr);
    _main_deletion_queue.push_function([=, this]() {
        vkDestroyPipelineLayout(_device, _gradient_pipeline_layout, nullptr);
        vkDestroyPipeline(_device, gradient_effect.pipeline, nullptr);
        vkDestroyPipeline(_device, sky_effect.pipeline, nullptr);
    });
}

void VulkanRenderer::init_mesh_pipeline()
{
    VkShaderModule triangle_frag_shader;
    if (!vkutil::load_shader_module(
            "../shaders/tex_image.frag.spv", _device, &triangle_frag_shader)) {
        fmt::println("Error building the triangle fragment shader module.");
    }

    VkShaderModule triangle_vertex_shader;
    if (!vkutil::load_shader_module(
            "../shaders/colored_triangle_mesh.vert.spv", _device, &triangle_vertex_shader)) {
        fmt::println("Error building the triangle vertex shader module.");
    }

    VkPushConstantRange buffer_range {};
    buffer_range.offset = 0;
    buffer_range.size = sizeof(GPUDrawPushConstants);
    buffer_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &buffer_range;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = &_single_image_descriptor_layout;
    pipeline_layout_info.setLayoutCount = 1;

    VK_CHECK(
        vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_mesh_pipeline_layout));

    PipelineBuilder pipeline_builder;
    pipeline_builder._pipeline_layout = _mesh_pipeline_layout;
    pipeline_builder.set_shaders(triangle_vertex_shader, triangle_frag_shader);
    pipeline_builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipeline_builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipeline_builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipeline_builder.set_multisampling_none();
    pipeline_builder.enable_blending_alphablend();
    pipeline_builder.enable_depth_test(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipeline_builder.set_color_attachment_format(_draw_image.image_format);
    pipeline_builder.set_depth_format(_depth_image.image_format);

    _mesh_pipeline = pipeline_builder.build_pipeline(_device);

    vkDestroyShaderModule(_device, triangle_frag_shader, nullptr);
    vkDestroyShaderModule(_device, triangle_vertex_shader, nullptr);

    _main_deletion_queue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _mesh_pipeline_layout, nullptr);
        vkDestroyPipeline(_device, _mesh_pipeline, nullptr);
    });
}

void VulkanRenderer::init_imgui()
{
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imgui_pool;
    VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imgui_pool));

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForVulkan(_window);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _instance;
    init_info.PhysicalDevice = _chosen_gpu;
    init_info.Device = _device;
    init_info.Queue = _graphics_queue;
    init_info.DescriptorPool = imgui_pool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.UseDynamicRendering = true;
    init_info.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO
    };
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchain_image_format;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture();

    _main_deletion_queue.push_function([=, this]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, imgui_pool, nullptr);
    });
}

void VulkanRenderer::init_default_data()
{
    uint32_t white = glm::packUnorm4x8(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    _white_image = create_image((void *)&white, VkExtent3D { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1.0f));
    _grey_image = create_image((void *)&grey, VkExtent3D { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    _black_image = create_image((void *)&black, VkExtent3D { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
    std::array<uint32_t, 16 * 16> pixels;
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[(y * 16) + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    _error_checkerboard_image = create_image(pixels.data(), VkExtent3D { 16, 16, 1 },
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampler_info = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(_device, &sampler_info, nullptr, &_default_sampler_nearest);

    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;

    vkCreateSampler(_device, &sampler_info, nullptr, &_default_sampler_linear);

    _main_deletion_queue.push_function([&]() {
        vkDestroySampler(_device, _default_sampler_nearest, nullptr);
        vkDestroySampler(_device, _default_sampler_linear, nullptr);

        destroy_image(_white_image);
        destroy_image(_grey_image);
        destroy_image(_black_image);
        destroy_image(_error_checkerboard_image);
    });

    GLTFMetallic_Roughness::MaterialResources material_resources;
    material_resources.color_image = _white_image;
    material_resources.color_sampler = _default_sampler_linear;
    material_resources.metal_rough_image = _white_image;
    material_resources.metal_rough_smapler = _default_sampler_linear;

    AllocatedBuffer material_constants =
        create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    GLTFMetallic_Roughness::MaterialConstants *scene_uniform_data =
        (GLTFMetallic_Roughness::MaterialConstants *)material_constants.info.pMappedData;
    scene_uniform_data->color_factors = glm::vec4 { 1.0f, 1.0f, 1.0f, 1.0f };
    scene_uniform_data->metal_rough_factors = glm::vec4 { 1.0f, 0.5f, 0.0f, 0.0f };

    _main_deletion_queue.push_function([=, this]() { destroy_buffer(material_constants); });

    material_resources.data_buffer = material_constants.buffer;
    material_resources.data_buffer_offset = 0;

    _default_data = _metal_rough_material.write_material(
        _device, MaterialPass::MainColor, material_resources, _global_descriptor_allocator);
}

void VulkanRenderer::init_object_buffers()
{
    for (std::size_t i = 0; i < FRAME_OVERLAP; i++) {
        _frames[i].object_buffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        _main_deletion_queue.push_function(
            [this, i]() { destroy_buffer(_frames[i].object_buffer); });

        _frames[i].indirect_buffer =
            create_buffer(sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);
        _main_deletion_queue.push_function(
            [this, i]() { destroy_buffer(_frames[i].indirect_buffer); });
    }
}

void VulkanRenderer::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchain_builder { _chosen_gpu, _device, _surface };

    _swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkb_swapchain =
        swapchain_builder
            .set_desired_format(VkSurfaceFormatKHR { .format = _swapchain_image_format,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

    _swapchain_extent = vkb_swapchain.extent;
    _swapchain = vkb_swapchain.swapchain;
    _swapchain_images = vkb_swapchain.get_images().value();
    _swapchain_image_views = vkb_swapchain.get_image_views().value();
}

AllocatedBuffer VulkanRenderer::create_buffer(
    size_t alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage)
{
    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_info.pNext = nullptr;
    buffer_info.size = alloc_size;
    buffer_info.usage = usage;

    VmaAllocationCreateInfo vma_alloc_info = {};
    vma_alloc_info.usage = memory_usage;
    vma_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer new_buffer;

    VK_CHECK(vmaCreateBuffer(_allocator, &buffer_info, &vma_alloc_info, &new_buffer.buffer,
        &new_buffer.allocation, &new_buffer.info));

    return new_buffer;
}

void VulkanRenderer::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    for (size_t i = 0; i < _swapchain_image_views.size(); i++) {
        vkDestroyImageView(_device, _swapchain_image_views[i], nullptr);
    }
}

void VulkanRenderer::destroy_buffer(const AllocatedBuffer &buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void VulkanRenderer::draw_background(VkCommandBuffer cmd)
{
    ComputeEffect &effect = _background_effects[_current_background_effect];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradient_pipeline_layout, 0, 1,
        &_draw_image_descriptors, 0, nullptr);

    vkCmdPushConstants(cmd, _gradient_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
        sizeof(ComputePushConstants), &effect.data);

    vkCmdDispatch(
        cmd, std::ceil(_draw_extent.width / 16.0f), std::ceil(_draw_extent.height / 16.0f), 1);
}

void VulkanRenderer::draw_geometry(
    VkCommandBuffer cmd, DrawContext &main_draw_context, Camera &main_camera, EngineStats &stats)
{
    stats.reset_counters();

    auto start_time = std::chrono::system_clock::now();

    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(main_draw_context.opaque_surfaces.size());
    for (uint32_t i = 0; i < main_draw_context.opaque_surfaces.size(); i++) {
        if (is_visible(main_draw_context.opaque_surfaces[i], scene_data.viewproj)) {
            opaque_draws.push_back(i);
        }
    }
    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](uint32_t i_a, uint32_t i_b) -> bool {
        const RenderObject &a = main_draw_context.opaque_surfaces[i_a];
        const RenderObject &b = main_draw_context.opaque_surfaces[i_b];

        if (a.material == b.material) {
            return a.index_buffer < b.index_buffer;
        }
        return a.material < b.material;
    });

    std::vector<uint32_t> transparent_draws;
    transparent_draws.reserve(main_draw_context.transparent_surfaces.size());
    for (uint32_t i = 0; i < main_draw_context.transparent_surfaces.size(); i++) {
        if (is_visible(main_draw_context.transparent_surfaces[i], scene_data.viewproj)) {
            transparent_draws.push_back(i);
        }
    }
    std::sort(transparent_draws.begin(), transparent_draws.end(),
        [&](uint32_t i_a, uint32_t i_b) -> bool {
            const RenderObject &a = main_draw_context.transparent_surfaces[i_a];
            const RenderObject &b = main_draw_context.transparent_surfaces[i_b];

            glm::vec3 pos_a = glm::vec3(a.transform * glm::vec4(a.bounds.origin, 1.0f));
            glm::vec3 pos_b = glm::vec3(b.transform * glm::vec4(b.bounds.origin, 1.0f));

            float dist_a = glm::distance(pos_a, main_camera.position);
            float dist_b = glm::distance(pos_b, main_camera.position);

            return dist_a > dist_b;
        });

    AllocatedBuffer gpu_scene_data_buffer = create_buffer(
        sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    get_current_frame().deletion_queue.push_function(
        [=, this]() { destroy_buffer(gpu_scene_data_buffer); });

    GPUSceneData *scene_uniform_data = (GPUSceneData *)gpu_scene_data_buffer.info.pMappedData;
    *scene_uniform_data = scene_data;

    VkDescriptorSet global_descriptor =
        get_current_frame().frame_descriptors.allocate(_device, _gpu_scene_data_descriptor_layout);

    DescriptorWriter writer;
    writer.write_buffer(0, gpu_scene_data_buffer.buffer, sizeof(GPUSceneData), 0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_buffer(1, get_current_frame().object_buffer.buffer,
        sizeof(GPUObjectData) * MAX_OBJECTS, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.update_set(_device, global_descriptor);

    VkRenderingAttachmentInfo color_attachment = vkinit::attachment_info(
        _draw_image.image_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depth_attachment = vkinit::depth_attachment_info(
        _depth_image.image_view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo render_info =
        vkinit::rendering_info(_draw_extent, &color_attachment, &depth_attachment);

    vkCmdBeginRendering(cmd, &render_info);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _mesh_pipeline);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _draw_extent.width;
    viewport.height = _draw_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _draw_extent.width;
    scissor.extent.height = _draw_extent.height;

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Get mapped pointer for the indirect buffer.
    VkDrawIndexedIndirectCommand *indirect_commands =
        (VkDrawIndexedIndirectCommand *)_frames[_frame_number % FRAME_OVERLAP]
            .indirect_buffer.info.pMappedData;

    // Track the batching state.
    MaterialInstance *last_material = nullptr;
    VkBuffer last_index_buffer = VK_NULL_HANDLE;
    uint32_t batch_start = 0;
    uint32_t batch_count = 0;

    auto flush_batch = [&]() {
        if (batch_count == 0)
            return;

        // Bind the state for this batch.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, last_material->pipeline->pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            last_material->pipeline->layout, 0, 1, &global_descriptor, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            last_material->pipeline->layout, 1, 1, &last_material->material_set, 0, nullptr);

        VkViewport vp = {
            .x = 0,
            .y = 0,
            .width = (float)_window_extent.width,
            .height = (float)_window_extent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D sc;
        sc.offset.x = 0;
        sc.offset.y = 0;
        sc.extent.width = _window_extent.width;
        sc.extent.height = _window_extent.height;
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindIndexBuffer(cmd, last_index_buffer, 0, VK_INDEX_TYPE_UINT32);

        uint32_t offset = batch_start * sizeof(VkDrawIndexedIndirectCommand);
        uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
        vkCmdDrawIndexedIndirect(
            cmd, get_current_frame().indirect_buffer.buffer, offset, batch_count, stride);
        stats.drawcall_count++;

        // Move the start index forward for the next batch.
        batch_start += batch_count;
        batch_count = 0;
    };

    // Copy the objects to the GPU.

    // Get mapped pointer for the SSBO.
    GPUObjectData *obj_buf =
        (GPUObjectData *)_frames[_frame_number % FRAME_OVERLAP].object_buffer.info.pMappedData;
    std::size_t obj_idx = 0;
    for (auto r_idx : opaque_draws) {
        obj_buf[obj_idx].world_matrix = main_draw_context.opaque_surfaces[r_idx].transform;
        obj_buf[obj_idx].vertex_buffer =
            main_draw_context.opaque_surfaces[r_idx].vertex_buffer_address;
        obj_idx++;
    }
    for (auto r_idx : transparent_draws) {
        obj_buf[obj_idx].world_matrix = main_draw_context.transparent_surfaces[r_idx].transform;
        obj_buf[obj_idx].vertex_buffer =
            main_draw_context.transparent_surfaces[r_idx].vertex_buffer_address;
        obj_idx++;
    }

    uint32_t current_instance = 0;
    auto process_draw_list = [&](const std::vector<uint32_t> &draws,
                                 const std::vector<RenderObject> &surfaces) {
        for (auto r_idx : draws) {
            const RenderObject &r = surfaces[r_idx];

            // If the material of index buffer chagnes,
            // the batch must be flushed and a new one must be be started.
            if (r.material != last_material || r.index_buffer != last_index_buffer) {
                flush_batch();
                last_material = r.material;
                last_index_buffer = r.index_buffer;
            }

            // Write the command for this specific object.
            indirect_commands[current_instance].indexCount = r.index_count;
            indirect_commands[current_instance].instanceCount = 1;
            indirect_commands[current_instance].firstIndex = r.first_index;
            indirect_commands[current_instance].vertexOffset = 0;
            indirect_commands[current_instance].firstInstance = current_instance;

            stats.triangle_count += r.index_count / 3;

            batch_count++;
            current_instance++;
        }

        flush_batch();
    };

    process_draw_list(opaque_draws, main_draw_context.opaque_surfaces);
    last_material = nullptr;
    last_index_buffer = VK_NULL_HANDLE;

    process_draw_list(transparent_draws, main_draw_context.transparent_surfaces);

    main_draw_context.opaque_surfaces.clear();
    main_draw_context.transparent_surfaces.clear();

    vkCmdEndRendering(cmd);

    auto end_time = std::chrono::system_clock::now();
    auto time_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    stats.mesh_draw_time = time_elapsed.count() / 1000.0f;
}

void VulkanRenderer::draw_imgui(
    VkCommandBuffer cmd, VkImageView target_image_view, EngineStats &stats)
{
    VkRenderingAttachmentInfo color_attachment = vkinit::attachment_info(
        target_image_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo render_info =
        vkinit::rendering_info(_swapchain_extent, &color_attachment, nullptr);

    vkCmdBeginRendering(cmd, &render_info);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void VulkanRenderer::immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function)
{
    VK_CHECK(vkResetFences(_device, 1, &_imm_fence));
    VK_CHECK(vkResetCommandBuffer(_imm_cmd_buffer, 0));

    VkCommandBuffer cmd = _imm_cmd_buffer;

    VkCommandBufferBeginInfo cmd_begin_info =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmd_info = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit_info = vkinit::submit_info(&cmd_info, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(_graphics_queue, 1, &submit_info, _imm_fence));

    VK_CHECK(vkWaitForFences(_device, 1, &_imm_fence, true, 9999999999));
}

GPUMeshBuffers VulkanRenderer::upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertex_buffer_size = vertices.size() * sizeof(Vertex);
    const size_t index_buffer_size = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers new_surface;

    new_surface.vertex_buffer = create_buffer(vertex_buffer_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    VkBufferDeviceAddressInfo device_address_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = new_surface.vertex_buffer.buffer
    };
    new_surface.vertex_buffer_address = vkGetBufferDeviceAddress(_device, &device_address_info);

    new_surface.index_buffer = create_buffer(index_buffer_size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging = create_buffer(vertex_buffer_size + index_buffer_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = staging.info.pMappedData;

    memcpy(data, vertices.data(), vertex_buffer_size);
    memcpy((char *)data + vertex_buffer_size, indices.data(), index_buffer_size);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertex_copy { 0 };
        vertex_copy.dstOffset = 0;
        vertex_copy.srcOffset = 0;
        vertex_copy.size = vertex_buffer_size;

        vkCmdCopyBuffer(cmd, staging.buffer, new_surface.vertex_buffer.buffer, 1, &vertex_copy);

        VkBufferCopy index_copy { 0 };
        index_copy.dstOffset = 0;
        index_copy.srcOffset = vertex_buffer_size;
        index_copy.size = index_buffer_size;

        vkCmdCopyBuffer(cmd, staging.buffer, new_surface.index_buffer.buffer, 1, &index_copy);
    });

    destroy_buffer(staging);

    return new_surface;
}

void VulkanRenderer::resize_swapchain()
{
    vkDeviceWaitIdle(_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _window_extent.width = w;
    _window_extent.height = h;

    create_swapchain(_window_extent.width, _window_extent.height);
}

AllocatedImage VulkanRenderer::create_image(
    VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage new_image;
    new_image.image_format = format;
    new_image.image_extent = size;

    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped) {
        img_info.mipLevels =
            static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(
        _allocator, &img_info, &alloc_info, &new_image.image, &new_image.allocation, nullptr));

    VkImageAspectFlags aspect_flag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspect_flag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo view_info =
        vkinit::imageview_create_info(format, new_image.image, aspect_flag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &new_image.image_view));

    return new_image;
}

AllocatedImage VulkanRenderer::create_image(
    void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    size_t data_size = size.depth * size.width * size.height * 4;
    AllocatedBuffer upload_buffer =
        create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(upload_buffer.info.pMappedData, data, data_size);

    AllocatedImage new_image = create_image(size, format,
        usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

    immediate_submit([&](VkCommandBuffer cmd) {
        vkutil::transition_image(
            cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy_region = {};
        copy_region.bufferOffset = 0;
        copy_region.bufferRowLength = 0;
        copy_region.bufferImageHeight = 0;

        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.imageSubresource.mipLevel = 0;
        copy_region.imageSubresource.baseArrayLayer = 0;
        copy_region.imageSubresource.layerCount = 1;
        copy_region.imageExtent = size;

        vkCmdCopyBufferToImage(cmd, upload_buffer.buffer, new_image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        if (mipmapped) {
            vkutil::generate_mipmaps(cmd, new_image.image,
                VkExtent2D { new_image.image_extent.width, new_image.image_extent.height });
        } else {
            vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    });

    destroy_buffer(upload_buffer);

    return new_image;
}

void VulkanRenderer::destroy_image(const AllocatedImage &img)
{
    vkDestroyImageView(_device, img.image_view, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
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
        mat_data.pipeline = &transparent_pipeline;
    } else {
        mat_data.pipeline = &opaque_pipeline;
    }

    mat_data.material_set = descriptor_allocator.allocate(device, material_layout);

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
