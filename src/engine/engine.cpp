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

static AutoCVar_Float cvar_main_fov(
    "r.main_fov", "FOV for the main camera", 70.0f, CVarFlags::EditFloatDrag);

void Engine::init()
{
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    constexpr int STARTUP_WIDTH = 1920;
    constexpr int STARTUP_HEIGHT = 1080;
    _window = SDL_CreateWindow("Engine Over Heaven", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, STARTUP_WIDTH, STARTUP_HEIGHT, window_flags);

    int w, h;
    SDL_Vulkan_GetDrawableSize(_window, &w, &h);
    renderer.init(_window, w, h);

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
        vkDeviceWaitIdle(renderer._gpu.device);

        // The scene's buffers and the glTFs free their GPU resources, so they must go before the
        // renderer.
        renderer.destroy_render_scene(render_scene);
        current_scene.gltfs.clear();

        renderer.cleanup();

        SDL_DestroyWindow(_window);
    }
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
            int w, h;
            SDL_Vulkan_GetDrawableSize(_window, &w, &h);
            renderer.resize_swapchain(w, h);
            _resize_requested = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Stats")) {
            ImGui::Text("Frametime: %f", stats.frametime);
            ImGui::Text("Scene Update Time: %f", stats.scene_update_time);
            ImGui::Text("Mesh Draw Time: %f", stats.render.mesh_draw_time);
            ImGui::Text("Total Triangles %i", stats.render.total_triangle_count);
            ImGui::Text("Visible Triangles %i", stats.render.visible_triangle_count);
            ImGui::Text("Draw calls %i", stats.render.drawcall_count);
        }
        ImGui::End();

        if (ImGui::Begin("Background")) {
            ComputeEffect &selected =
                renderer._background_effects[renderer._current_background_effect];

            ImGui::Text("Selected effect: %s", selected.name);

            ImGui::SliderInt("Effect Index", &renderer._current_background_effect, 0,
                renderer._background_effects.size() - 1);

            ImGui::InputFloat4("data1", (f32 *)&selected.data.data1);
            ImGui::InputFloat4("data2", (f32 *)&selected.data.data2);
            ImGui::InputFloat4("data3", (f32 *)&selected.data.data3);
            ImGui::InputFloat4("data4", (f32 *)&selected.data.data4);
        }
        ImGui::End();

        if (ImGui::Begin("Scene")) {
            const char *preview =
                current_scene_index >= 0 ? scene_definitions[current_scene_index].name.c_str() : "";
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

        renderer.draw(render_scene, main_camera, stats.render, _resize_requested);

        auto frame_end = std::chrono::system_clock::now();
        auto frame_elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
        stats.frametime = frame_elapsed / 1000.0f;
    }
}

void Engine::update_scene()
{
    auto start_time = std::chrono::system_clock::now();

    f32 dt = stats.frametime / 1000.0f;
    if (dt <= 0.0f) {
        dt = 1.0f / 60.0f;
    }
    camera_controller.update(dt);

    glm::mat4 view = main_camera.get_view_matrix();
    f32 fov_deg = std::clamp(static_cast<f32>(cvar_main_fov.get()), 10.0f, 170.0f);
    const VkExtent2D extent = renderer._swapchain_extent;
    glm::mat4 projection = glm::perspective(
        glm::radians(fov_deg), (f32)extent.width / (f32)extent.height, 10000.0f, 0.1f);
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
    vkDeviceWaitIdle(renderer._gpu.device);

    renderer.destroy_render_scene(render_scene);
    current_scene.gltfs.clear();

    current_scene.load_data(&renderer, scene_definitions[index]);

    DrawContext surfaces;
    current_scene.collect_surfaces(surfaces);
    render_scene = renderer.init_render_scene(surfaces);

    current_scene_index = index;
}
