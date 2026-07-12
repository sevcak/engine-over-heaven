// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <camera.h>
#include <vk_descriptors.h>
#include <vk_loader.h>
#include <vk_types.h>

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

struct GLTFMetallic_Roughness
{
    MaterialPipeline opaque_pipeline;
    MaterialPipeline transparent_pipeline;

    VkDescriptorSetLayout material_layout;

    struct MaterialConstants
    {
        glm::vec4 color_factors;
        glm::vec4 metal_rough_factors;
        // Padding
        glm::vec4 extra[14];
    };

    struct MaterialResources
    {
        AllocatedImage color_image;
        VkSampler color_sampler;
        AllocatedImage metal_rough_image;
        VkSampler metal_rough_smapler;
        VkBuffer data_buffer;
        uint32_t data_buffer_offset;
    };

    DescriptorWriter writer;

    void build_pipelines(VulkanEngine *engine);

    void clear_resources(VkDevice device);

    MaterialInstance write_material(VkDevice device, MaterialPass pass,
        const MaterialResources &resources, DescriptorAllocatorGrowable &descriptor_allocator);
};

struct MeshNode : public Node
{
    std::shared_ptr<MeshAsset> mesh;

    virtual void draw(const glm::mat4 &top_matrix, DrawContext &ctx) override;
};

struct RenderObject
{
    uint32_t index_count;
    uint32_t first_index;
    VkBuffer index_buffer;

    MaterialInstance *material;

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
    int triangle_count;
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

class VulkanEngine
{
public:
    bool _is_initialized { false };
    bool _stop_rendering { false };
    bool _resize_requested { false };

    int _frame_number { 0 };

    EngineStats stats;

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
    float _render_scale = 1.0f;

    DescriptorAllocatorGrowable _global_descriptor_allocator;

    VkDescriptorSet _draw_image_descriptors;
    VkDescriptorSetLayout _draw_image_descriptor_layout;

    VkDescriptorSetLayout _single_image_descriptor_layout;

    GPUSceneData scene_data;

    VkDescriptorSetLayout _gpu_scene_data_descriptor_layout;

    VkPipelineLayout _gradient_pipeline_layout;

    VkPipelineLayout _mesh_pipeline_layout;
    VkPipeline _mesh_pipeline;

    VkFence _imm_fence;
    VkCommandBuffer _imm_cmd_buffer;
    VkCommandPool _imm_cmd_pool;

    std::vector<ComputeEffect> _background_effects;
    int _current_background_effect { 0 };

    std::vector<std::shared_ptr<MeshAsset>> _test_meshes;

    AllocatedImage _white_image;
    AllocatedImage _black_image;
    AllocatedImage _grey_image;
    AllocatedImage _error_checkerboard_image;

    VkSampler _default_sampler_linear;
    VkSampler _default_sampler_nearest;

    MaterialInstance _default_data;
    GLTFMetallic_Roughness _metal_rough_material;

    DrawContext main_draw_context;
    std::unordered_map<std::string, std::shared_ptr<Node>> loaded_nodes;

    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loaded_scenes;

    Camera main_camera;

    static VulkanEngine &get();

    // initializes everything in the engine
    void init();

    // shuts down the engine
    void cleanup();

    // draw loop
    void draw();

    // run main loop
    void run();

    /*
     * Send commands to the GPU without syncronizing with the swapchain or the
     * rendering logic. Uses a fence and a command buffer different from the one
     * used in draw.
     */
    void immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function);

    GPUMeshBuffers upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

    void update_scene();

    AllocatedBuffer create_buffer(
        size_t alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);

    AllocatedImage create_image(
        VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedImage create_image(void *data, VkExtent3D size, VkFormat format,
        VkImageUsageFlags usage, bool mipmapped = false);

    void destroy_buffer(const AllocatedBuffer &buffer);

    void destroy_image(const AllocatedImage &img);

private:
    void init_vulkan();

    void init_swapchain();

    void init_commands();

    void init_sync_structures();

    void init_descriptors();

    void init_pipelines();

    void init_background_pipelines();

    void init_mesh_pipeline();

    void init_imgui();

    void init_default_data();

    void create_swapchain(uint32_t width, uint32_t height);

    void destroy_swapchain();

    void draw_background(VkCommandBuffer cmd);

    void draw_geometry(VkCommandBuffer cmd);

    void draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view);

    void resize_swapchain();
};
