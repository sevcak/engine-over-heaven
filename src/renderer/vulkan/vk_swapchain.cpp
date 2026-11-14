#include "vk_swapchain.hpp"

#include "VkBootstrap.h"
#include "vk_initializers.hpp"

void Swapchain::init(const GpuContext &gpu, u32 width, u32 height)
{
    vkb::SwapchainBuilder swapchain_builder { gpu.physical_device, gpu.device, gpu.surface };

    image_format = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkb_swapchain =
        swapchain_builder
            .set_desired_format(VkSurfaceFormatKHR {
                .format = image_format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

    extent = vkb_swapchain.extent;
    handle = vkb_swapchain.swapchain;
    images = vkb_swapchain.get_images().value();
    image_views = vkb_swapchain.get_image_views().value();

    render_semaphores.resize(images.size());
    for (usize i = 0; i < render_semaphores.size(); i++) {
        VkSemaphoreCreateInfo semaphore_create_info = vkinit::semaphore_create_info();
        VK_CHECK(
            vkCreateSemaphore(gpu.device, &semaphore_create_info, nullptr, &render_semaphores[i]));
    }
}

void Swapchain::destroy(const GpuContext &gpu)
{
    for (auto &semaphore : render_semaphores) {
        vkDestroySemaphore(gpu.device, semaphore, nullptr);
    }
    for (auto &image_view : image_views) {
        vkDestroyImageView(gpu.device, image_view, nullptr);
    }
    vkDestroySwapchainKHR(gpu.device, handle, nullptr);
}

void Swapchain::recreate(const GpuContext &gpu, u32 width, u32 height)
{
    // The GPU may still be using old images and semaphores, so wait until it is idle.
    vkDeviceWaitIdle(gpu.device);

    destroy(gpu);
    init(gpu, width, height);
}