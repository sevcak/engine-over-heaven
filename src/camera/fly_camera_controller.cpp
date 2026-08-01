#include <camera/fly_camera_controller.hpp>

#include <SDL.h>
#include <algorithm>
#include <glm/trigonometric.hpp>
#include <imgui.h>

FlyCameraController::FlyCameraController(Camera *cam) : camera(cam) {}

void FlyCameraController::set_captured(bool captured)
{
    is_captured = captured;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
}

void FlyCameraController::toggle_captured()
{
    set_captured(!is_captured);
}

void FlyCameraController::process_sdl_event(const SDL_Event &e)
{
    ImGuiIO &io = ImGui::GetIO();

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
        if (!io.WantCaptureMouse) {
            set_captured(true);
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
        if (is_captured) {
            set_captured(false);
        }
    } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        if (e.key.keysym.sym == SDLK_F1 || e.key.keysym.sym == SDLK_TAB) {
            toggle_captured();
        } else if (e.key.keysym.sym == SDLK_ESCAPE && is_captured) {
            set_captured(false);
        }
    }

    if (is_captured && e.type == SDL_MOUSEMOTION) {
        if (camera) {
            camera->yaw += static_cast<float>(e.motion.xrel) * mouse_sensitivity;
            camera->pitch -= static_cast<float>(e.motion.yrel) * mouse_sensitivity;

            const float pitch_limit = glm::radians(89.0f);
            camera->pitch = std::clamp(camera->pitch, -pitch_limit, pitch_limit);
        }
    }
}

void FlyCameraController::update(float dt)
{
    if (!camera || dt <= 0.0f) {
        return;
    }

    if (!is_captured) {
        return;
    }

    const Uint8 *state = SDL_GetKeyboardState(nullptr);

    glm::vec3 move_dir(0.0f);
    glm::vec3 forward = camera->get_forward_vector();
    glm::vec3 right = camera->get_right_vector();
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    if (state[SDL_SCANCODE_W]) {
        move_dir += forward;
    }
    if (state[SDL_SCANCODE_S]) {
        move_dir -= forward;
    }
    if (state[SDL_SCANCODE_D]) {
        move_dir += right;
    }
    if (state[SDL_SCANCODE_A]) {
        move_dir -= right;
    }
    if (state[SDL_SCANCODE_E] || state[SDL_SCANCODE_SPACE]) {
        move_dir += up;
    }
    if (state[SDL_SCANCODE_Q] || state[SDL_SCANCODE_LCTRL]) {
        move_dir -= up;
    }

    if (glm::length(move_dir) > 0.001f) {
        move_dir = glm::normalize(move_dir);

        float current_speed = move_speed;
        if (state[SDL_SCANCODE_LSHIFT]) {
            current_speed *= sprint_multiplier;
        }

        camera->position += move_dir * current_speed * dt;
    }
}
