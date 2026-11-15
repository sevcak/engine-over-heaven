#include "vk_renderer.hpp"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <logger/logger.hpp>
#include <numeric>
#include <vk_gpu_data.hpp>
#include <vk_images.hpp>
#include <vk_initializers.hpp>
#include <vk_pipelines.hpp>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "VkBootstrap.h"
#include "vk_types.hpp"

#include "cvars.hpp"

static AutoCVar_Float cvar_render_scale(
    "r.render_scale", "Resolution scale for rendering", 1.0f, CVarFlags::EditFloatDrag);

void RenderStats::reset_counters()
{
    drawcall_count = 0;
    visible_triangle_count = 0;
    total_triangle_count = 0;
}

void VulkanRenderer::init(SDL_Window *window, u32 width, u32 height)
{
    _window = window;

    _gpu.init(_window);
    _swapchain.init(_gpu, width, height);
    create_render_targets({ width, height });
    init_commands();
    init_sync_structures();
    init_descriptors();
    _background.init(_gpu, _global_descriptor_allocator, _draw_image.image_view);
    _cull.init(_gpu, _global_descriptor_allocator, _frames);
    init_pipelines();
    _default_resources.init(_gpu, _material_system, _global_descriptor_allocator);
    init_object_buffers();
    _imgui_pool = init_imgui(_gpu, _window, _swapchain.image_format);

    _is_initialized = true;
}

void VulkanRenderer::cleanup()
{
    if (!_is_initialized) {
        return;
    }

    vkDeviceWaitIdle(_gpu.device);

    destroy_imgui(_gpu, _imgui_pool);
    destroy_object_buffers();
    _default_resources.destroy(_gpu);
    destroy_pipelines();
    _cull.destroy(_gpu);
    _background.destroy(_gpu);
    destroy_descriptors();
    destroy_sync_structures();
    destroy_commands();
    destroy_render_targets();
    _swapchain.destroy(_gpu);
    _gpu.destroy();

    _is_initialized = false;
}

void VulkanRenderer::draw(
    RenderScene &scene, Camera &main_camera, RenderStats &stats, bool &resize_requested)
{
    // Wait until the GPU has finished rendering the last frame.
    VK_CHECK(vkWaitForFences(_gpu.device, 1, &get_current_frame().render_fence, true, 1000000000));

    u32 swapchain_image_index;
    VkResult e = vkAcquireNextImageKHR(_gpu.device, _swapchain.handle, 1000000000,
        get_current_frame().swapchain_semaphore, nullptr, &swapchain_image_index);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
        return;
    } else if (e != VK_SUCCESS && e != VK_SUBOPTIMAL_KHR) {
        OH_LOG_FATAL("Failed to acquire swapchain image: {}", string_VkResult(e));
        abort();
    }

    // Reset only once this frame is sure to submit, which signals the fence again. Resetting before
    // a failed acquire would leave the fence unsignaled, and the next wait on it would time out.
    VK_CHECK(vkResetFences(_gpu.device, 1, &get_current_frame().render_fence));

    VkCommandBuffer cmd = get_current_frame().main_command_buffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmd_begin_info =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

    f32 render_scale = cvar_render_scale.get();
    u32 base_width = std::min(_swapchain.extent.width, _draw_image.image_extent.width);
    u32 base_height = std::min(_swapchain.extent.height, _draw_image.image_extent.height);
    _draw_extent.width = std::max(1u, static_cast<u32>(base_width * render_scale));
    _draw_extent.height = std::max(1u, static_cast<u32>(base_height * render_scale));

    // Transition the main draw image into general layout so we can compute-write into it.
    vkutil::transition_image(
        cmd, _draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    _background.record(cmd, _draw_extent);

    // Transition the draw and depth images into correct layouts for the graphics pipeline.
    vkutil::transition_image(
        cmd, _draw_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _depth_image.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    FrameData &frame = get_current_frame();

    // Read back the visible triangle count the cull pass wrote the last time this frame ran.
    stats.reset_counters();
    stats.total_triangle_count = scene.total_triangle_count;
    GPURenderStats *gpu_stats = (GPURenderStats *)frame.render_stats_buffer.info.pMappedData;
    stats.visible_triangle_count = gpu_stats->visible_triangles;
    memset(gpu_stats, 0, sizeof(GPURenderStats));

    if (scene.total_triangle_count > 0) {
        auto start_time = std::chrono::system_clock::now();

        // The cull pass reads the transparent order, so it must be uploaded first.
        sort_transparent_back_to_front(scene, main_camera.position);
        memcpy(frame.transparent_order_buffer.info.pMappedData,
            scene.transparent_render_order.data(),
            scene.transparent_render_order.size() * sizeof(u32));
        VkDescriptorSet scene_data_set = create_scene_data_set(scene);

        _cull.record(_gpu, cmd, frame, scene, scene_data.viewproj);
        stats.drawcall_count = record_forward_pass(cmd, scene, frame.indirect_buffer.buffer,
            scene_data_set, _draw_image.image_view, _depth_image.image_view, _draw_extent);

        auto end_time = std::chrono::system_clock::now();
        auto time_elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        stats.mesh_draw_time = time_elapsed.count() / 1000.0f;
    }

    // Transition the draw image and the swapchain image into their correct transfer layouts.
    vkutil::transition_image(cmd, _draw_image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, _swapchain.images[swapchain_image_index],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy from the draw image into the swapchain.
    vkutil::copy_image_to_image(cmd, _draw_image.image, _swapchain.images[swapchain_image_index],
        _draw_extent, _swapchain.extent);

    // Transition the swapchain image so we can draw into it.
    vkutil::transition_image(cmd, _swapchain.images[swapchain_image_index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Draw ImGui into the swapchain image.
    record_imgui(cmd, _swapchain.image_views[swapchain_image_index], _swapchain.extent);

    // Set swapchain image layout to Present so it can be shown on the screen.
    vkutil::transition_image(cmd, _swapchain.images[swapchain_image_index],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmd_info = vkinit::command_buffer_submit_info(cmd);

    VkSemaphoreSubmitInfo wait_info =
        vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            get_current_frame().swapchain_semaphore);
    VkSemaphore signal_semaphore = _swapchain.render_semaphores[swapchain_image_index];
    VkSemaphoreSubmitInfo signal_info =
        vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, signal_semaphore);

    VkSubmitInfo2 submit_info = vkinit::submit_info(&cmd_info, &signal_info, &wait_info);

    VK_CHECK(
        vkQueueSubmit2(_gpu.graphics_queue, 1, &submit_info, get_current_frame().render_fence));

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.pSwapchains = &_swapchain.handle;
    present_info.swapchainCount = 1;

    present_info.pWaitSemaphores = &signal_semaphore;
    present_info.waitSemaphoreCount = 1;
    present_info.pImageIndices = &swapchain_image_index;

    VkResult present_result = vkQueuePresentKHR(_gpu.graphics_queue, &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
        return;
    } else if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
        OH_LOG_FATAL("Failed to present swapchain image: {}", string_VkResult(present_result));
        abort();
    }

    _frame_number++;
}

void VulkanRenderer::create_render_targets(VkExtent2D extent)
{
    VkExtent3D draw_image_extent = { extent.width, extent.height, 1 };

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

    vmaCreateImage(_gpu.allocator, &rimg_info, &rimg_allocinfo, &_draw_image.image,
        &_draw_image.allocation, nullptr);

    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(
        _draw_image.image_format, _draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(_gpu.device, &rview_info, nullptr, &_draw_image.image_view));

    _depth_image.image_format = VK_FORMAT_D32_SFLOAT;
    _depth_image.image_extent = draw_image_extent;

    VkImageUsageFlags depth_image_usages {};
    depth_image_usages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info =
        vkinit::image_create_info(_depth_image.image_format, depth_image_usages, draw_image_extent);

    vmaCreateImage(_gpu.allocator, &dimg_info, &rimg_allocinfo, &_depth_image.image,
        &_depth_image.allocation, nullptr);

    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(
        _depth_image.image_format, _depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(_gpu.device, &dview_info, nullptr, &_depth_image.image_view));
}

void VulkanRenderer::destroy_render_targets()
{
    vkDestroyImageView(_gpu.device, _draw_image.image_view, nullptr);
    vmaDestroyImage(_gpu.allocator, _draw_image.image, _draw_image.allocation);

    vkDestroyImageView(_gpu.device, _depth_image.image_view, nullptr);
    vmaDestroyImage(_gpu.allocator, _depth_image.image, _depth_image.allocation);
}

void VulkanRenderer::init_commands()
{
    VkCommandPoolCreateInfo command_pool_info = vkinit::command_pool_create_info(
        _gpu.graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(
            _gpu.device, &command_pool_info, nullptr, &_frames[i].command_pool));

        VkCommandBufferAllocateInfo cmd_alloc_info =
            vkinit::command_buffer_allocate_info(_frames[i].command_pool, 1);

        VK_CHECK(vkAllocateCommandBuffers(
            _gpu.device, &cmd_alloc_info, &_frames[i].main_command_buffer));
    }
}

void VulkanRenderer::destroy_commands()
{
    // Destroying a pool also frees the command buffers allocated from it.
    for (FrameData &frame : _frames) {
        vkDestroyCommandPool(_gpu.device, frame.command_pool, nullptr);
    }
}

void VulkanRenderer::init_sync_structures()
{
    VkFenceCreateInfo fence_create_info = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphore_create_info = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(_gpu.device, &fence_create_info, nullptr, &_frames[i].render_fence));
        VK_CHECK(vkCreateSemaphore(
            _gpu.device, &semaphore_create_info, nullptr, &_frames[i].swapchain_semaphore));
    }
}

void VulkanRenderer::destroy_sync_structures()
{
    for (FrameData &frame : _frames) {
        vkDestroySemaphore(_gpu.device, frame.swapchain_semaphore, nullptr);
        vkDestroyFence(_gpu.device, frame.render_fence, nullptr);
    }
}

void VulkanRenderer::init_descriptors()
{
    // The storage image entry is for the background pass's draw image set.
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 }
    };

    _global_descriptor_allocator.init(_gpu.device, 10, sizes);

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 }
        };

        _frames[i].frame_descriptors = DescriptorAllocatorGrowable {};
        _frames[i].frame_descriptors.init(_gpu.device, 1000, frame_sizes);
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        _gpu_scene_data_descriptor_layout =
            builder.build(_gpu.device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }
}

void VulkanRenderer::destroy_descriptors()
{
    vkDestroyDescriptorSetLayout(_gpu.device, _gpu_scene_data_descriptor_layout, nullptr);

    for (FrameData &frame : _frames) {
        frame.frame_descriptors.destroy_pools(_gpu.device);
    }

    _global_descriptor_allocator.destroy_pools(_gpu.device);
}

void VulkanRenderer::init_pipelines()
{
    _material_system.register_type<GLTFMetallic_Roughness>();
    _material_system.build_pipelines(
        _gpu, {
                  .color_format = _draw_image.image_format,
                  .depth_format = _depth_image.image_format,
                  .scene_data_layout = _gpu_scene_data_descriptor_layout,
              });
}

void VulkanRenderer::destroy_pipelines()
{
    _material_system.clear_resources(_gpu.device);
}

void VulkanRenderer::init_object_buffers()
{
    for (usize i = 0; i < FRAME_OVERLAP; i++) {
        _frames[i].indirect_buffer =
            _gpu.create_buffer(sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

        _frames[i].render_stats_buffer = _gpu.create_buffer(sizeof(GPURenderStats),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

        _frames[i].transparent_order_buffer = _gpu.create_buffer(sizeof(u32) * MAX_OBJECTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        _frames[i].scene_data_buffer = _gpu.create_buffer(
            sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
}

void VulkanRenderer::destroy_object_buffers()
{
    for (FrameData &frame : _frames) {
        _gpu.destroy_buffer(frame.transparent_order_buffer);
        _gpu.destroy_buffer(frame.render_stats_buffer);
        _gpu.destroy_buffer(frame.indirect_buffer);
        _gpu.destroy_buffer(frame.scene_data_buffer);
    }
}

VkDescriptorSet VulkanRenderer::create_scene_data_set(const RenderScene &scene)
{
    AllocatedBuffer &scene_data_buffer = get_current_frame().scene_data_buffer;

    GPUSceneData *scene_uniform_data = (GPUSceneData *)scene_data_buffer.info.pMappedData;
    *scene_uniform_data = scene_data;

    VkDescriptorSet scene_data_set = get_current_frame().frame_descriptors.allocate(
        _gpu.device, _gpu_scene_data_descriptor_layout);

    DescriptorWriter writer;
    writer.write_buffer(
        0, scene_data_buffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_buffer(
        1, scene.object_buffer.buffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.update_set(_gpu.device, scene_data_set);

    return scene_data_set;
}

void VulkanRenderer::resize_swapchain(u32 width, u32 height)
{
    _swapchain.recreate(_gpu, width, height);
}
