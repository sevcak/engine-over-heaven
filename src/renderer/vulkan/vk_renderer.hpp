#pragma once

#include "vk_context.hpp"
#include "vk_default_resources.hpp"
#include "vk_descriptors.hpp"
#include "vk_frame.hpp"
#include "vk_gpu_data.hpp"
#include "vk_imgui.hpp"
#include "vk_material_system.hpp"
#include "vk_render_scene.hpp"
#include "vk_swapchain.hpp"
#include "vk_types.hpp"

#include "passes/background_pass.hpp"
#include "passes/cull_pass.hpp"
#include "passes/forward_pass.hpp"

#include <camera/camera.hpp>
#include <core/types.hpp>
#include <vector>
#include <vulkan/vulkan_core.h>

struct SDL_Window;

struct RenderStats
{
    int visible_triangle_count;
    int total_triangle_count;
    int drawcall_count;
    f32 mesh_draw_time;

    void reset_counters();
};

class VulkanRenderer
{
public:
    bool _is_initialized { false };

    int _frame_number { 0 };

    struct SDL_Window *_window { nullptr };

    GpuContext _gpu;

    Swapchain _swapchain;

    FrameData _frames[FRAME_OVERLAP];

    FrameData &get_current_frame() { return _frames[_frame_number % FRAME_OVERLAP]; }

    AllocatedImage _draw_image;
    AllocatedImage _depth_image;

    VkExtent2D _draw_extent;

    DescriptorAllocatorGrowable _global_descriptor_allocator;

    GPUSceneData scene_data;

    VkDescriptorSetLayout _gpu_scene_data_descriptor_layout;

    BackgroundPass _background;
    CullPass _cull;

    /** The debug UI backend's descriptor pool, from `init_imgui`. */
    VkDescriptorPool _imgui_pool { VK_NULL_HANDLE };

    MaterialSystem _material_system;
    DefaultResources _default_resources;

    void init(SDL_Window *window, u32 width, u32 height);

    void cleanup();

    void draw(RenderScene &scene, Camera &main_camera, RenderStats &stats, bool &resize_requested);

    void resize_swapchain(u32 width, u32 height);

private:
    /**
     * @brief Create the offscreen draw image and depth image that the scene renders into.
     *
     * They are created once at startup and not rebuilt on resize. `draw()` clamps the draw extent
     * to their size instead.
     *
     * @param extent The size of both images in pixels.
     */
    void create_render_targets(VkExtent2D extent);

    /** Destroy the draw and depth images. The GPU must be idle. */
    void destroy_render_targets();

    /** Create each frame's command pool and its main command buffer. */
    void init_commands();
    /** Destroy each frame's command pool, which frees its command buffer. The GPU must be idle. */
    void destroy_commands();

    /** Create each frame's render fence (signaled) and swapchain-acquire semaphore. */
    void init_sync_structures();
    /** Destroy each frame's render fence and swapchain-acquire semaphore. The GPU must be idle. */
    void destroy_sync_structures();

    /** Create the global and per-frame descriptor allocators and the scene-data set layout. */
    void init_descriptors();
    /**
     * Destroy the scene-data set layout and every descriptor pool. Everything with sets from the
     * global allocator must already be destroyed.
     */
    void destroy_descriptors();

    /** Register the material types and build their pipelines. */
    void init_pipelines();
    /** Destroy the material pipelines and layouts. The GPU must be idle. */
    void destroy_pipelines();

    /** Create each frame's indirect, render-stats and transparent-order buffers. */
    void init_object_buffers();
    /** Destroy each frame's indirect, render-stats and transparent-order buffers. */
    void destroy_object_buffers();

    /**
     * @brief Upload this frame's `scene_data` and build the set that every material pipeline binds
     * as set 0.
     *
     * Creates a new uniform buffer every frame and frees it a frame later through the frame's
     * deletion queue. Step 7 of the refactor replaces this with a persistent per-frame buffer.
     *
     * @param scene The scene whose object buffer goes into binding 1.
     * @return The set, allocated from the current frame's descriptor allocator.
     */
    VkDescriptorSet create_scene_data_set(const RenderScene &scene);
};
