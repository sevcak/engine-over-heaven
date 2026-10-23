add_library(scene STATIC
    scene.cpp
    scene.hpp
)

set_property(TARGET scene PROPERTY CXX_STANDARD 20)
target_include_directories(scene PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(scene PUBLIC vulkan_renderer)
