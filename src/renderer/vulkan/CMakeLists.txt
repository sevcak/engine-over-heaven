add_library(vulkan_renderer STATIC
    vk_renderer.cpp
    vk_renderer.hpp
    vk_descriptors.cpp
    vk_descriptors.hpp
    vk_images.cpp
    vk_images.hpp
    vk_initializers.cpp
    vk_initializers.hpp
    vk_pipelines.cpp
    vk_pipelines.hpp
    vk_loader.cpp
    vk_loader.hpp
    vk_material_system.cpp
    vk_material_system.hpp
    vk_types.hpp
)

set_property(TARGET vulkan_renderer PROPERTY CXX_STANDARD 20)
target_compile_definitions(vulkan_renderer PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(vulkan_renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vulkan_renderer PUBLIC
    core
    camera
    vma
    glm
    Vulkan::Vulkan
    fmt::fmt
    stb_image
    SDL2::SDL2
    vkbootstrap
    imgui
    fastgltf::fastgltf
)
