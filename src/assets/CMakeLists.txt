add_library(assets STATIC
    gltf_loader.cpp
    gltf_loader.hpp
)

set_property(TARGET assets PROPERTY CXX_STANDARD 20)
target_include_directories(assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(assets PUBLIC vulkan_renderer logger PRIVATE fastgltf::fastgltf stb_image)
