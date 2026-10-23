#pragma once

#include "vk_descriptors.hpp"
#include "vk_frame.hpp"
#include "vk_gpu_data.hpp"
#include "vk_material_system.hpp"
#include "vk_render_scene.hpp"
#include "vk_types.hpp"
#include <camera/camera.hpp>
#include <core/types.hpp>
#include <functional>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>

struct SDL_Window;

struct ComputePushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect
{
    const char *name;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    ComputePushConstants data;
};

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

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debug_messenger;
    VkPhysicalDevice _chosen_gpu;
    VkDevice _device;
    VkSurfaceKHR _surface;

    VkSwapchainKHR _swapchain;
    VkFormat _swapchain_image_format;

    std::vector<VkImage> _swapchain_images;
    std::vector<VkImageView> _swapchain_image_views;
    VkExtent2D _swapchain_extent;

    std::vector<VkSemaphore> _render_semaphores;

    FrameData _frames[FRAME_OVERLAP];

    FrameData &get_current_frame() { return _frames[_frame_number % FRAME_OVERLAP]; }

    VkQueue _graphics_queue;
    u32 _graphics_queue_family;

    DeletionQueue _main_deletion_queue;

    VmaAllocator _allocator;

    AllocatedImage _draw_image;
    AllocatedImage _depth_image;

    VkExtent2D _draw_extent;

    DescriptorAllocatorGrowable _global_descriptor_allocator;

    VkDescriptorSet _draw_image_descriptors;
    VkDescriptorSetLayout _draw_image_descriptor_layout;

    GPUSceneData scene_data;

    VkDescriptorSetLayout _gpu_scene_data_descriptor_layout;

    VkDescriptorSetLayout _compute_descriptor_layout;

    VkPipelineLayout _gradient_pipeline_layout;

    VkPipelineLayout _cull_pipeline_layout;
    VkPipeline _cull_pipeline;

    VkFence _imm_fence;
    VkCommandBuffer _imm_cmd_buffer;
    VkCommandPool _imm_cmd_pool;

    std::vector<ComputeEffect> _background_effects;
    int _current_background_effect { 0 };

    AllocatedImage _white_image;
    AllocatedImage _black_image;
    AllocatedImage _grey_image;
    AllocatedImage _error_checkerboard_image;

    VkSampler _default_sampler_linear;
    VkSampler _default_sampler_nearest;

    MaterialInstance _default_data;
    MaterialSystem _material_system;

    /* Static render scene data. */
    std::vector<u32> transparent_render_order;
    std::vector<f32> center_distances_sq;

    void init(SDL_Window *window, u32 width, u32 height);

    RenderScene init_render_scene(DrawContext &ctx);

    /** Frees the scene's GPU buffers. The GPU must be idle. Safe to call on an empty scene. */
    void destroy_render_scene(RenderScene &scene);

    void cleanup();

    void draw(
        const RenderScene &scene, Camera &main_camera, RenderStats &stats, bool &resize_requested);

    void immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function);

    GPUMeshBuffers upload_mesh(std::span<u32> indices, std::span<Vertex> vertices);

    AllocatedBuffer create_buffer(
        usize alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);

    AllocatedImage create_image(
        VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedImage create_image(void *data, VkExtent3D size, VkFormat format,
        VkImageUsageFlags usage, bool mipmapped = false);

    void destroy_buffer(const AllocatedBuffer &buffer);
    void destroy_image(const AllocatedImage &img);

    void resize_swapchain(u32 width, u32 height);

private:
    constexpr static usize MAX_OBJECTS = 100000;

    void init_vulkan();

    void init_swapchain(u32 width, u32 height);

    void init_commands();

    void init_sync_structures();

    void init_descriptors();

    void init_pipelines();

    void init_background_pipelines();

    void init_compute_pipeline();

    void init_imgui();

    void init_default_data();

    void init_object_buffers();

    void create_swapchain(u32 width, u32 height);

    void destroy_swapchain();

    AllocatedBuffer upload_to_device_local(const void *data, usize size, VkBufferUsageFlags usage);

    void draw_background(VkCommandBuffer cmd);

    void draw_geometry(
        VkCommandBuffer cmd, const RenderScene &scene, Camera &main_camera, RenderStats &stats);

    void draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view, RenderStats &stats);
};
