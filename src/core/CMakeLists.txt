add_library(core STATIC
    camera.cpp
    camera.h
    cvars.cpp
    cvars.hpp
    string_utils.hpp
)

set_property(TARGET core PROPERTY CXX_STANDARD 20)
target_include_directories(core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(core PUBLIC glm SDL2::SDL2 imgui)
