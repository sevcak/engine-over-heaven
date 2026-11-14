#pragma once

#include "vk_context.hpp"

#include <vector>

/**
 * The window's swapchain and everything sized by its image count: the images, their views and one
 * render-finished semaphore per image. Owned by `VulkanRenderer`, recreated when the window
 * resizes.
 */
struct Swapchain
{
    VkSwapchainKHR handle;
    VkFormat image_format;
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    VkExtent2D extent;
    /**
     * One per swapchain image, indexed by the acquired image index. The frame's submit signals it
     * and the present waits on it.
     */
    std::vector<VkSemaphore> render_semaphores;

    /**
     * @brief Create the swapchain, its image views and one render semaphore per image.
     *
     * @param gpu The GPU context whose device and surface the swapchain is created for.
     * @param width The desired width in pixels. The surface may pick a different `extent`.
     * @param height The desired height in pixels.
     */
    void init(const GpuContext &gpu, u32 width, u32 height);

    /** Destroy everything `init` created. The GPU must be done with the images and semaphores. */
    void destroy(const GpuContext &gpu);

    /**
     * @brief Wait for the device to go idle, then rebuild the swapchain at a new size.
     *
     * @param gpu The GPU context the swapchain was created with.
     * @param width The desired new width in pixels.
     * @param height The desired new height in pixels.
     */
    void recreate(const GpuContext &gpu, u32 width, u32 height);
};
