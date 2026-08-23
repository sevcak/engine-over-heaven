add_library(engine_lib STATIC
    engine.cpp
    engine.hpp
)

set_property(TARGET engine_lib PROPERTY CXX_STANDARD 20)
target_include_directories(engine_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(engine_lib PUBLIC core camera vulkan_renderer)
