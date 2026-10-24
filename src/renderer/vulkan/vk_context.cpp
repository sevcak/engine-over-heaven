#include "vk_context.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "VkBootstrap.h"
#include "vk_images.hpp"
#include "vk_initializers.hpp"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

constexpr bool use_validation_layers = true;

void GpuContext::init(SDL_Window *window)
{
    vkb::InstanceBuilder builder;

    auto inst_ret = builder.set_app_name("Vulkan Application")
                        .request_validation_layers(use_validation_layers)
                        .use_default_debug_messenger()
                        .require_api_version(1, 3, 0)
                        .build();

    vkb::Instance vkb_inst = inst_ret.value();

    instance = vkb_inst.instance;
    debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(window, instance, &surface);

    VkPhysicalDeviceFeatures features {};
    features.multiDrawIndirect = true;
    features.drawIndirectFirstInstance = true;

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
    vkb::PhysicalDevice vkb_physical_device = selector.set_minimum_version(1, 3)
                                                  .set_required_features(features)
                                                  .set_required_features_13(features13)
                                                  .set_required_features_12(features12)
                                                  .set_surface(surface)
                                                  .select()
                                                  .value();

    vkb::DeviceBuilder device_builder { vkb_physical_device };

    vkb::Device vkb_device = device_builder.build().value();

    device = vkb_device.device;
    physical_device = vkb_physical_device.physical_device;

    graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.physicalDevice = physical_device;
    allocator_info.device = device;
    allocator_info.instance = instance;
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocator_info, &allocator);

    VkCommandPoolCreateInfo command_pool_info = vkinit::command_pool_create_info(
        graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &imm_cmd_pool));

    VkCommandBufferAllocateInfo cmd_alloc_info =
        vkinit::command_buffer_allocate_info(imm_cmd_pool, 1);
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc_info, &imm_cmd_buffer));

    VkFenceCreateInfo fence_create_info = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VK_CHECK(vkCreateFence(device, &fence_create_info, nullptr, &imm_fence));
}

void GpuContext::destroy()
{
    vkDestroyFence(device, imm_fence, nullptr);
    vkDestroyCommandPool(device, imm_cmd_pool, nullptr);

    vmaDestroyAllocator(allocator);

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);

    vkb::destroy_debug_utils_messenger(instance, debug_messenger);
    vkDestroyInstance(instance, nullptr);
}

void GpuContext::immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function) const
{
    VK_CHECK(vkResetFences(device, 1, &imm_fence));
    VK_CHECK(vkResetCommandBuffer(imm_cmd_buffer, 0));

    VkCommandBuffer cmd = imm_cmd_buffer;

    VkCommandBufferBeginInfo cmd_begin_info =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmd_info = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit_info = vkinit::submit_info(&cmd_info, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(graphics_queue, 1, &submit_info, imm_fence));

    VK_CHECK(vkWaitForFences(device, 1, &imm_fence, true, 9999999999));
}

AllocatedBuffer GpuContext::create_buffer(
    usize alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage) const
{
    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_info.pNext = nullptr;
    buffer_info.size = alloc_size;
    buffer_info.usage = usage;

    VmaAllocationCreateInfo vma_alloc_info = {};
    vma_alloc_info.usage = memory_usage;
    vma_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    vma_alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    AllocatedBuffer new_buffer;

    VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &vma_alloc_info, &new_buffer.buffer,
        &new_buffer.allocation, &new_buffer.info));

    return new_buffer;
}

void GpuContext::destroy_buffer(const AllocatedBuffer &buffer) const
{
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}

AllocatedImage GpuContext::create_image(
    VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) const
{
    AllocatedImage new_image;
    new_image.image_format = format;
    new_image.image_extent = size;

    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped) {
        img_info.mipLevels =
            static_cast<u32>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(
        allocator, &img_info, &alloc_info, &new_image.image, &new_image.allocation, nullptr));

    VkImageAspectFlags aspect_flag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspect_flag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo view_info =
        vkinit::imageview_create_info(format, new_image.image, aspect_flag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &new_image.image_view));

    return new_image;
}

AllocatedImage GpuContext::create_image(
    void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) const
{
    usize data_size = size.depth * size.width * size.height * 4;
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

void GpuContext::destroy_image(const AllocatedImage &image) const
{
    vkDestroyImageView(device, image.image_view, nullptr);
    vmaDestroyImage(allocator, image.image, image.allocation);
}

GPUMeshBuffers GpuContext::upload_mesh(std::span<u32> indices, std::span<Vertex> vertices) const
{
    const usize vertex_buffer_size = vertices.size() * sizeof(Vertex);
    const usize index_buffer_size = indices.size() * sizeof(u32);

    GPUMeshBuffers new_surface;

    new_surface.vertex_buffer = create_buffer(vertex_buffer_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    VkBufferDeviceAddressInfo device_address_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = new_surface.vertex_buffer.buffer
    };
    new_surface.vertex_buffer_address = vkGetBufferDeviceAddress(device, &device_address_info);

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

AllocatedBuffer GpuContext::upload_to_device_local(
    const void *data, usize size, VkBufferUsageFlags usage) const
{
    AllocatedBuffer staging_buffer =
        create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    memcpy(staging_buffer.info.pMappedData, data, size);

    AllocatedBuffer device_local_buffer =
        create_buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy { .srcOffset = 0, .dstOffset = 0, .size = size };
        vkCmdCopyBuffer(cmd, staging_buffer.buffer, device_local_buffer.buffer, 1, &copy);
    });

    destroy_buffer(staging_buffer);

    return device_local_buffer;
}
