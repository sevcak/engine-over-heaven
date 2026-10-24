#pragma once

#include "core/types.hpp"
#include "vk_types.hpp"
#include <functional>
#include <span>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

/**
 * The Vulkan device and everything needed to create GPU resources with it: instance, device,
 * queue, VMA allocator and a command buffer for blocking uploads. Owned by `VulkanRenderer`.
 */
struct GpuContext
{
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    u32 graphics_queue_family;
    VmaAllocator allocator;

    /** Command pool, buffer and fence used by `immediate_submit`. */
    VkCommandPool imm_cmd_pool;
    VkCommandBuffer imm_cmd_buffer;
    VkFence imm_fence;

    /**
     * @brief Create the instance, surface, device, queue, allocator and immediate-submit objects.
     *
     * @param window Window to create the presentation surface for.
     */
    void init(SDL_Window *window);

    /** Destroy everything `init` created. Every resource created through this context must be
     * destroyed first. */
    void destroy();

    /**
     * @brief Record commands into the immediate command buffer, submit them and wait until the GPU
     * has finished executing them.
     *
     * Blocking, so only use it at load time, never per frame.
     *
     * @param function Callback that records the commands.
     */
    void immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function) const;

    /**
     * @brief Create a persistently mapped buffer.
     *
     * @param alloc_size Size of the buffer in bytes.
     * @param usage Vulkan buffer usage flags.
     * @param memory_usage VMA memory usage, which decides where the memory lives.
     * @return The new buffer.
     */
    AllocatedBuffer create_buffer(
        usize alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage) const;

    void destroy_buffer(const AllocatedBuffer &buffer) const;

    /**
     * @brief Create an uninitialized device-local image and its view.
     *
     * @param size Extent of the image.
     * @param format Pixel format. `VK_FORMAT_D32_SFLOAT` creates a depth view.
     * @param usage Vulkan image usage flags.
     * @param mipmapped Allocate a full mip chain.
     * @return The new image.
     */
    AllocatedImage create_image(
        VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false) const;

    /**
     * @brief Create a device-local image, upload RGBA8 pixel data into it and transition it for
     * shader reads. Blocks until the upload is done.
     *
     * @param data Tightly packed pixels, 4 bytes per pixel.
     * @param size Extent of the image.
     * @param format Pixel format.
     * @param usage Vulkan image usage flags. Transfer usage is added automatically.
     * @param mipmapped Allocate and generate a full mip chain.
     * @return The new image.
     */
    AllocatedImage create_image(void *data, VkExtent3D size, VkFormat format,
        VkImageUsageFlags usage, bool mipmapped = false) const;

    void destroy_image(const AllocatedImage &image) const;

    /**
     * @brief Upload a mesh into device-local vertex and index buffers. Blocks until the upload is
     * done.
     *
     * @param indices Index data.
     * @param vertices Vertex data, read by the shaders through the buffer device address.
     * @return The new buffers and the vertex buffer's device address.
     */
    GPUMeshBuffers upload_mesh(std::span<u32> indices, std::span<Vertex> vertices) const;

    /**
     * @brief Copy data into a new device-local buffer through a staging buffer. Blocks until the
     * upload is done.
     *
     * @param data Data to upload.
     * @param size Size of the data in bytes.
     * @param usage Vulkan buffer usage flags. Transfer usage is added automatically.
     * @return The new buffer.
     */
    AllocatedBuffer upload_to_device_local(
        const void *data, usize size, VkBufferUsageFlags usage) const;
};
