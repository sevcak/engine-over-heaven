#include <vk_types.h>
#include <SDL_events.h>

class Camera {
public:
    glm::vec3 velocity;
    glm::vec3 position;
    float pitch { 0.0f };
    float yaw { 0.0f };

    glm::mat4 get_view_matrix();
    glm::mat4 get_rotation_matrix();

    void process_sdl_event(SDL_Event &e);

    void update();
};


