#pragma once

#include <SDL_events.h>
#include <camera/camera.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>
#include <vk_descriptors.h>
#include <vk_material_system.hpp>
#include <vk_types.h>
#include <vulkan/vulkan_core.h>

struct SDL_Window;
class VulkanRenderer;

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

    AllocatedBuffer object_buffer;
    AllocatedBuffer indirect_buffer;
    AllocatedBuffer render_stats_buffer;

    VkDescriptorSet compute_descriptor;
};

struct CullPushConstants
{
    glm::vec4 frustum_planes[6];
    uint32_t object_count;
    uint32_t cull_enabled;

    /**
     * Extract frustum planes from the view-projection matrix and store them into
     * `.frustum_planes`.
     *
     * Frustum planes are stored in the following order:
     *     - `0`: Left plane
     *     - `1`: Right plane
     *     - `2`: Bottom plane
     *     - `3`: Top plane
     *     - `4`: Near plane
     *     - `5`: Far plane
     *
     * @param vp View-projection matrix.
     */
    void extract_frustum_planes(const glm::mat4 &vp);
};

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

struct GPUSceneData
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::vec4 ambient_color;
    /* `sunlight_direction`:
     *	- `.xyz`: Direction
     *  - `.w`: Sun power
     */
    glm::vec4 sunlight_direction;
    glm::vec4 sunlight_color;
};

struct RenderObject
{
    uint32_t index_count;
    uint32_t first_index;
    VkBuffer index_buffer;

    MaterialInstance *material;
    Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertex_buffer_address;
};

struct DrawContext
{
    std::vector<RenderObject> opaque_surfaces;
    std::vector<RenderObject> transparent_surfaces;
};

struct EngineStats
{
    float frametime;
    int visible_triangle_count;
    int total_triangle_count;
    int drawcall_count;
    float scene_update_time;
    float mesh_draw_time;

    void reset_counters();
};

/*
 * The count of frames in flight.
 * Determines how many command lists the CPU can prepare ahead of time.
 */
constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanRenderer
{
public:
    bool _is_initialized { false };

    int _frame_number { 0 };

    VkExtent2D _window_extent { 1920, 1080 };

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
    uint32_t _graphics_queue_family;

    DeletionQueue _main_deletion_queue;

    VmaAllocator _allocator;

    AllocatedImage _draw_image;
    AllocatedImage _depth_image;

    VkExtent2D _draw_extent;

    DescriptorAllocatorGrowable _global_descriptor_allocator;

    VkDescriptorSet _draw_image_descriptors;
    VkDescriptorSetLayout _draw_image_descriptor_layout;

    VkDescriptorSetLayout _single_image_descriptor_layout;

    GPUSceneData scene_data;

    VkDescriptorSetLayout _gpu_scene_data_descriptor_layout;

    VkDescriptorSetLayout _compute_descriptor_layout;

    VkPipelineLayout _gradient_pipeline_layout;

    VkPipelineLayout _mesh_pipeline_layout;
    VkPipeline _mesh_pipeline;

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
    GLTFMetallic_Roughness _metal_rough_material;

    void init(SDL_Window *window, uint32_t width, uint32_t height);
    void cleanup();

    void draw(DrawContext &main_draw_context, Camera &main_camera, EngineStats &stats,
        bool &resize_requested);

    void immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function);

    GPUMeshBuffers upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

    AllocatedBuffer create_buffer(
        size_t alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);

    AllocatedImage create_image(
        VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedImage create_image(void *data, VkExtent3D size, VkFormat format,
        VkImageUsageFlags usage, bool mipmapped = false);

    void destroy_buffer(const AllocatedBuffer &buffer);
    void destroy_image(const AllocatedImage &img);

    void resize_swapchain();

private:
    constexpr static std::size_t MAX_OBJECTS = 100000;

    void init_vulkan();

    void init_swapchain();

    void init_commands();

    void init_sync_structures();

    void init_descriptors();

    void init_pipelines();

    void init_background_pipelines();

    void init_mesh_pipeline();

    void init_compute_pipeline();

    void init_imgui();

    void init_default_data();

    void init_object_buffers();

    void create_swapchain(uint32_t width, uint32_t height);

    void destroy_swapchain();

    void draw_background(VkCommandBuffer cmd);

    void draw_geometry(VkCommandBuffer cmd, DrawContext &main_draw_context, Camera &main_camera,
        EngineStats &stats);

    void draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view, EngineStats &stats);
};
