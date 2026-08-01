#pragma once

#include <camera.h>
#include <vk_renderer.hpp>
#include <vk_loader.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

class Engine
{
public:
    bool _is_initialized { false };
    bool _stop_rendering { false };
    bool _resize_requested { false };

    int _frame_number { 0 };

    EngineStats stats;

    VkExtent2D _window_extent { 1920, 1080 };

    SDL_Window *_window { nullptr };

    VulkanRenderer renderer;

    DrawContext main_draw_context;
    std::unordered_map<std::string, std::shared_ptr<Node>> loaded_nodes;
    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loaded_scenes;
    std::vector<std::shared_ptr<MeshAsset>> _test_meshes;

    Camera main_camera;

    static Engine &get();

    void init();
    void cleanup();
    void run();
    void update_scene();
};
