#pragma once

/**
 * @file
 * Per-frame-in-flight resources. The renderer owns `FRAME_OVERLAP` of these and cycles through
 * them, so the CPU can record one frame while the GPU is still working on the previous one.
 */

#include "vk_descriptors.hpp"
#include "vk_types.hpp"

#include <deque>
#include <functional>

/**
 * The count of frames in flight.
 * Determines how many command lists the CPU can prepare ahead of time.
 */
constexpr unsigned int FRAME_OVERLAP = 2;

struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()> &&function) { deletors.push_back(function); }

    void flush()
    {
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
            (*it)();
        }

        deletors.clear();
    }
};

struct FrameData
{
    VkCommandPool command_pool;
    VkCommandBuffer main_command_buffer;

    VkSemaphore swapchain_semaphore;
    VkFence render_fence;

    DeletionQueue deletion_queue;

    DescriptorAllocatorGrowable frame_descriptors;

    AllocatedBuffer indirect_buffer;
    AllocatedBuffer render_stats_buffer;

    /**
     * Buffer storing the order of transparent objects for correct rendering.
     */
    AllocatedBuffer transparent_order_buffer;

    VkDescriptorSet compute_descriptor;
};
