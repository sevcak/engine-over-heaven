#include "engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <chrono>
#include <thread>
#include <algorithm>
#include <cassert>

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "cvars.hpp"

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
    main_camera.position = glm::vec3(30.f, -00.f, -085.f);
    main_camera.pitch = 0.0f;
    main_camera.yaw = 0.0f;

    _test_meshes = load_gltf_meshes(&renderer, "../assets/basicmesh.glb").value();

    for (auto &m : _test_meshes) {
        std::shared_ptr<MeshNode> new_node = std::make_shared<MeshNode>();
        new_node->mesh = m;

        new_node->local_transform = glm::mat4 { 1.0f };
        new_node->world_transform = glm::mat4 { 1.0f };

        for (auto &s : new_node->mesh->surfaces) {
            s.material = std::make_shared<GLTFMaterial>(renderer._default_data);
        }

        loaded_nodes[m->name] = std::move(new_node);
    }

    std::string structure_path = { "./../assets/structure.glb" };
    auto structure_file = load_gltf(&renderer, structure_path);

    assert(structure_file.has_value());

    loaded_scenes["structure"] = *structure_file;

    _is_initialized = true;
}

void Engine::cleanup()
{
    if (_is_initialized) {
        vkDeviceWaitIdle(renderer._device);

        loaded_scenes.clear();
        loaded_nodes.clear();

        for (auto &mesh : _test_meshes) {
            renderer.destroy_buffer(mesh->mesh_buffers.index_buffer);
            renderer.destroy_buffer(mesh->mesh_buffers.vertex_buffer);
        }

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

            main_camera.process_sdl_event(e);

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
            ImGui::Text("Triangles %i", stats.triangle_count);
            ImGui::Text("Draw calls %i", stats.drawcall_count);
        }
        ImGui::End();

        if (ImGui::Begin("Background")) {
            ComputeEffect &selected = renderer._background_effects[renderer._current_background_effect];

            ImGui::Text("Selected effect: %s", selected.name);

            ImGui::SliderInt(
                "Effect Index", &renderer._current_background_effect, 0, renderer._background_effects.size() - 1);

            ImGui::InputFloat4("data1", (float *)&selected.data.data1);
            ImGui::InputFloat4("data2", (float *)&selected.data.data2);
            ImGui::InputFloat4("data3", (float *)&selected.data.data3);
            ImGui::InputFloat4("data4", (float *)&selected.data.data4);
        }
        ImGui::End();

        CVarSystem::get()->draw_imgui_editor();

        ImGui::Render();

        update_scene();

        renderer.draw(main_draw_context, main_camera, stats, _resize_requested);

        auto frame_end = std::chrono::system_clock::now();
        auto frame_elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
        stats.frametime = frame_elapsed / 1000.0f;
    }
}

void Engine::update_scene()
{
    auto start_time = std::chrono::system_clock::now();

    main_camera.update();

    glm::mat4 view = main_camera.get_view_matrix();
    float fov_deg = std::clamp(static_cast<float>(cvar_main_fov.get()), 10.0f, 170.0f);
    glm::mat4 projection = glm::perspective(glm::radians(fov_deg),
        (float)renderer._window_extent.width / (float)renderer._window_extent.height, 10000.0f, 0.1f);
    projection[1][1] *= -1;

    renderer.scene_data.view = view;
    renderer.scene_data.proj = projection;
    renderer.scene_data.viewproj = projection * view;

    renderer.scene_data.ambient_color = glm::vec4(0.1f);
    renderer.scene_data.sunlight_color = glm::vec4(1.0f);
    renderer.scene_data.sunlight_direction = glm::vec4(0.0f, 1.0f, 0.5f, 1.0f);

    main_draw_context.opaque_surfaces.clear();

    if (loaded_nodes.contains("Suzanne")) {
        loaded_nodes["Suzanne"]->draw(glm::mat4 { 1.0f }, main_draw_context);
    }

    for (int i = -3; i < 3; i++) {
        if (loaded_nodes.contains("Cube")) {
            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3 { 0.2f });
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3 { i, 1.0f, 0.0f });

            loaded_nodes["Cube"]->draw(translation * scale, main_draw_context);
        }
    }

    if (loaded_scenes.contains("structure")) {
        loaded_scenes["structure"]->draw(glm::mat4 { 1.0f }, main_draw_context);
    }

    auto end_time = std::chrono::system_clock::now();
    auto time_elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    stats.scene_update_time = time_elapsed.count() / 1000.0f;
}