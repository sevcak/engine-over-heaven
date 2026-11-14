#pragma once

/**
 * @file
 * The Vulkan and SDL backends of the debug UI. ImGui keeps its state in a global context, so these
 * are free functions. The renderer owns the descriptor pool, and the engine builds the UI.
 */

#include "vk_context.hpp"

struct SDL_Window;

/**
 * @brief Create the ImGui context, set up its SDL and Vulkan backends and upload the font texture.
 *
 * @param gpu The GPU context the Vulkan backend renders with.
 * @param window The window the SDL backend reads input from.
 * @param swapchain_format The format of the images the UI is drawn into. Taken by reference
 * because the backend keeps a pointer to it, so it must outlive the backend.
 * @return The descriptor pool the Vulkan backend allocates from. Pass it to `destroy_imgui`.
 */
VkDescriptorPool init_imgui(
    const GpuContext &gpu, SDL_Window *window, const VkFormat &swapchain_format);

/**
 * @brief Shut down the Vulkan backend and destroy its descriptor pool. The GPU must be idle.
 *
 * @param gpu The GPU context `init_imgui` was called with.
 * @param pool The pool `init_imgui` returned.
 */
void destroy_imgui(const GpuContext &gpu, VkDescriptorPool pool);

/**
 * @brief Record the UI that the engine built this frame.
 *
 * The target must be in `COLOR_ATTACHMENT_OPTIMAL` layout. Its contents are kept and the UI is
 * drawn on top.
 *
 * @param cmd The command buffer to record into.
 * @param target_view The image to draw into, normally the acquired swapchain image.
 * @param extent The size of the target in pixels.
 */
void record_imgui(VkCommandBuffer cmd, VkImageView target_view, VkExtent2D extent);
