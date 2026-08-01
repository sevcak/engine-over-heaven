#pragma once

#include <camera/camera.hpp>
#include <SDL_events.h>

class FlyCameraController {
public:
    Camera *camera { nullptr };

    float move_speed { 15.0f };
    float sprint_multiplier { 2.5f };
    float mouse_sensitivity { 0.0025f };

    bool is_captured { false };

    explicit FlyCameraController(Camera *cam = nullptr);

    void process_sdl_event(const SDL_Event &e);
    void update(float dt);

    void set_captured(bool captured);
    void toggle_captured();
};
