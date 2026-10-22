#pragma once

#include <SDL_events.h>
#include <camera/camera.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>
#include <vk_descriptors.hpp>
#include <vk_material_system.hpp>
#include <vk_types.hpp>
#include <vulkan/vulkan_core.h>
#include <filesystem>
#include <memory>
#include <string>

struct SDL_Window;
class VulkanRenderer;
struct LoadedGLTF;

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

struct CullPushConstants
{
    glm::vec4 frustum_planes[6];
    uint32_t object_count;
    uint32_t cull_enabled;
    uint32_t first_transparent_index;

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
    glm::vec4 camera_position;
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

/**
 * Persistent result of building a scene. Built once when the scene loads; per frame only the
 * camera-dependent work (culling, transparent sort) touches it.
 *
 * Slots: every surface gets a slot number at build time. Slot `i` is the object's permanent
 * identity. All per-draw arrays below are indexed by slot, and the object buffer is never
 * reordered. Draw order is changed by reordering the indirect commands instead, which point back to
 * a slot through `firstInstance`.
 *
 * Slot layout: [0, first_transparent_index) is opaque, sorted once by (material, index buffer).
 * [first_transparent_index, end) is transparent, re-sorted by camera distance every frame.
 *
 * Does not own the meshes, textures or materials the pointers refer to. Those live in the
 * `LoadedGLTF`s in `Scene`, which must outlive this.
 */
struct RenderScene
{
    /**
     * Device-local array of `GPUObjectData` (world matrix, bounding sphere, vertex buffer address),
     * one per slot. Uploaded once at load, read-only on the GPU afterwards.
     */
    AllocatedBuffer object_buffer;

    /** Index buffer of the draw at each slot. Read by draw recording for vkCmdBindIndexBuffer. */
    std::vector<VkBuffer> index_buffers;
    // TODO: We should consider storing indices here instead for better control and potential memory savings.
    /** Material of the draw at each slot. Read by draw recording to decide when to rebind. */
    std::vector<MaterialInstance *> materials;
    /** First transparent slot. Equals the opaque draw count. */
    uint32_t first_transparent_index = 0;

    /**
     * World-space center of each transparent object (indexed by `slot - first_transparent_index`).
     * Computed once at load. The per-frame transparent sort uses it to get the camera distance.
     */
    std::vector<glm::vec3> transparent_centers;

    /**
     * Triangles in the whole scene, every draw counted once whether visible or not. Computed once
     * at load, since it never changes. Reported as the "total triangles" stat.
     */
    uint32_t total_triangle_count = 0;

    /**
     * Device-local `VkDrawIndexedIndirectCommand` per slot, uploaded once. Read-only input 
     * to the shader, which copies the visible ones into the current frame's output buffer.
     */
    AllocatedBuffer input_command_buffer;
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

/**
 * What a scene is made of. Plain data with no GPU resources, so it is cheap to copy and to keep a
 * list of.
 */
struct SceneDefinition
{
    std::string name;
    std::vector<std::filesystem::path> gltf_paths;
    /** One per glTF, in the same order as `gltf_paths`. Missing entries mean identity. */
    std::vector<glm::mat4> root_transforms;
};

/**
 * A loaded `SceneDefinition`. Owns the glTFs (and through them the meshes, textures and materials)
 * that a `RenderScene` points into, so it must outlive the `RenderScene` built from it.
 */
struct Scene
{
    std::vector<std::unique_ptr<LoadedGLTF>> gltfs;
    /** Parallel to `gltfs`. */
    std::vector<glm::mat4> root_transforms;

    /** Replaces the loaded glTFs with the ones in `definition`. glTFs that fail to load are skipped. */
    void load_data(VulkanRenderer *renderer, const SceneDefinition &definition);
};

/**
 * The count of frames in flight.
 * Determines how many command lists the CPU can prepare ahead of time.
 */
constexpr unsigned int FRAME_OVERLAP = 2;

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
    uint32_t _graphics_queue_family;

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
    std::vector<uint32_t> transparent_render_order;
    std::vector<float> center_distances_sq;

    void init(SDL_Window *window, uint32_t width, uint32_t height);

    RenderScene init_render_scene(Scene &scene);

    /** Frees the scene's GPU buffers. The GPU must be idle. Safe to call on an empty scene. */
    void destroy_render_scene(RenderScene &scene);

    void cleanup();

    void draw(const RenderScene &scene, Camera &main_camera, EngineStats &stats,
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

    void resize_swapchain(uint32_t width, uint32_t height);

private:
    constexpr static std::size_t MAX_OBJECTS = 100000;

    void init_vulkan();

    void init_swapchain(uint32_t width, uint32_t height);

    void init_commands();

    void init_sync_structures();

    void init_descriptors();

    void init_pipelines();

    void init_background_pipelines();

    void init_compute_pipeline();

    void init_imgui();

    void init_default_data();

    void init_object_buffers();

    void create_swapchain(uint32_t width, uint32_t height);

    void destroy_swapchain();

    AllocatedBuffer upload_to_device_local(
        const void *data, std::size_t size, VkBufferUsageFlags usage);

    void draw_background(VkCommandBuffer cmd);

    void draw_geometry(VkCommandBuffer cmd, const RenderScene &scene, Camera &main_camera,
        EngineStats &stats);

    void draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view, EngineStats &stats);
};
