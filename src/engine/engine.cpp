#include "engine.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "logger/logger.hpp"

#include <cvars.hpp>

Engine *loaded_engine = nullptr;

static AutoCVar_Float cvar_main_fov(
    "r.main_fov", "FOV for the main camera", 70.0f, CVarFlags::EditFloatDrag);

Engine &Engine::get()
{
    return *loaded_engine;
}

void Engine::init()
{
    assert(loaded_engine == nullptr);
    loaded_engine = this;

    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    _window = SDL_CreateWindow("Engine Over Heaven", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, _window_extent.width, _window_extent.height, window_flags);

    renderer.init(_window, _window_extent.width, _window_extent.height);

    main_camera.velocity = glm::vec3(0.0f);
    main_camera.position = glm::vec3(0.0f, 3.0f, 0.0f);
    main_camera.pitch = 0.0f;
    main_camera.yaw = 0.0f;
    camera_controller = FlyCameraController(&main_camera);

    scene_definitions.push_back({
        .name = "Structure",
        .gltf_paths = { "./../assets/structure.glb" },
    });
    scene_definitions.push_back({
        .name = "Sponza",
        .gltf_paths = {
            "./../custom_assets/sponza/main_sponza/NewSponza_Main_glTF_003.gltf",
            "./../custom_assets/sponza/pkg_a_curtains/NewSponza_Curtains_glTF.gltf",
        },
    });

    load_scene(0);

    _is_initialized = true;
}

void Engine::cleanup()
{
    if (_is_initialized) {
        vkDeviceWaitIdle(renderer._device);

        // The scene's buffers and the glTFs free their GPU resources, so they must go before the
        // renderer.
        renderer.destroy_render_scene(render_scene);
        current_scene.gltfs.clear();

        renderer.cleanup();

        SDL_DestroyWindow(_window);
    }

    loaded_engine = nullptr;
}

void Engine::run()
{
    SDL_Event e;
    bool quit = false;

    while (!quit) {
        auto frame_start = std::chrono::system_clock::now();

        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT)
                quit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    _stop_rendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    _stop_rendering = false;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    _resize_requested = true;
                }
            }

            camera_controller.process_sdl_event(e);

            ImGui_ImplSDL2_ProcessEvent(&e);
        }

        if (_stop_rendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (_resize_requested) {
            renderer.resize_swapchain();
            _resize_requested = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Stats")) {
            ImGui::Text("Frametime: %f", stats.frametime);
            ImGui::Text("Mesh Draw Time: %f", stats.mesh_draw_time);
            ImGui::Text("Scene Update Time: %f", stats.scene_update_time);
            ImGui::Text("Total Triangles %i", stats.total_triangle_count);
            ImGui::Text("Visible Triangles %i", stats.visible_triangle_count);
            ImGui::Text("Draw calls %i", stats.drawcall_count);
        }
        ImGui::End();

        if (ImGui::Begin("Background")) {
            ComputeEffect &selected =
                renderer._background_effects[renderer._current_background_effect];

            ImGui::Text("Selected effect: %s", selected.name);

            ImGui::SliderInt("Effect Index", &renderer._current_background_effect, 0,
                renderer._background_effects.size() - 1);

            ImGui::InputFloat4("data1", (float *)&selected.data.data1);
            ImGui::InputFloat4("data2", (float *)&selected.data.data2);
            ImGui::InputFloat4("data3", (float *)&selected.data.data3);
            ImGui::InputFloat4("data4", (float *)&selected.data.data4);
        }
        ImGui::End();

        if (ImGui::Begin("Scene")) {
            const char *preview = current_scene_index >= 0
                ? scene_definitions[current_scene_index].name.c_str()
                : "";
            if (ImGui::BeginCombo("Scene", preview)) {
                for (int i = 0; i < static_cast<int>(scene_definitions.size()); i++) {
                    const bool is_selected = (i == current_scene_index);
                    if (ImGui::Selectable(scene_definitions[i].name.c_str(), is_selected)) {
                        requested_scene_index = i;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::End();

        CVarSystem::get()->draw_imgui_editor();

        ImGui::Render();

        if (requested_scene_index != -1 && requested_scene_index != current_scene_index) {
            load_scene(requested_scene_index);
        }
        requested_scene_index = -1;

        update_scene();

        renderer.draw(render_scene, main_camera, stats, _resize_requested);

        auto frame_end = std::chrono::system_clock::now();
        auto frame_elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
        stats.frametime = frame_elapsed / 1000.0f;
    }
}

void Engine::update_scene()
{
    auto start_time = std::chrono::system_clock::now();

    float dt = stats.frametime / 1000.0f;
    if (dt <= 0.0f) {
        dt = 1.0f / 60.0f;
    }
    camera_controller.update(dt);

    glm::mat4 view = main_camera.get_view_matrix();
    float fov_deg = std::clamp(static_cast<float>(cvar_main_fov.get()), 10.0f, 170.0f);
    glm::mat4 projection = glm::perspective(glm::radians(fov_deg),
        (float)renderer._window_extent.width / (float)renderer._window_extent.height, 10000.0f,
        0.1f);
    projection[1][1] *= -1;

    renderer.scene_data.camera_position = glm::vec4(main_camera.position, 1.0f);

    renderer.scene_data.view = view;
    renderer.scene_data.proj = projection;
    renderer.scene_data.viewproj = projection * view;

    renderer.scene_data.ambient_color = glm::vec4(0.1f);
    renderer.scene_data.sunlight_color = glm::vec4(1.0f);
    renderer.scene_data.sunlight_direction = glm::vec4(0.0f, 1.0f, 0.5f, 1.0f);

    auto end_time = std::chrono::system_clock::now();
    auto time_elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    stats.scene_update_time = time_elapsed.count() / 1000.0f;
}

void Engine::load_scene(int index)
{
    // The GPU may still be using the old scene's buffers and textures.
    vkDeviceWaitIdle(renderer._device);

    renderer.destroy_render_scene(render_scene);
    current_scene.gltfs.clear();

    current_scene.load_data(&renderer, scene_definitions[index]);
    render_scene = renderer.init_render_scene(current_scene);
    current_scene_index = index;
}
