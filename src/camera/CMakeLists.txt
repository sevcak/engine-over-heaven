add_library(camera STATIC
    camera.cpp
    camera.hpp
    fly_camera_controller.cpp
    fly_camera_controller.hpp
)

set_property(TARGET camera PROPERTY CXX_STANDARD 20)
target_include_directories(camera PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(camera PUBLIC glm SDL2::SDL2 imgui)
