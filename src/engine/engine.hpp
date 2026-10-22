#pragma once

#include "vk_loader.hpp"
#include "vk_renderer.hpp"
#include <camera/camera.hpp>
#include <camera/fly_camera_controller.hpp>
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

    EngineStats stats;

    SDL_Window *_window { nullptr };

    VulkanRenderer renderer;

    std::vector<SceneDefinition> scene_definitions;
    int current_scene_index { -1 };
    /** Set by the UI. Acted on once per frame, outside of the UI code. */
    int requested_scene_index { -1 };

    Scene current_scene;

    RenderScene render_scene;

    Camera main_camera;
    FlyCameraController camera_controller;

    void init();
    void cleanup();
    void run();
    void update_scene();
    /** Unloads the current scene, if any, and loads `scene_definitions[index]`. */
    void load_scene(int index);
};
